#!/usr/bin/env python3
"""Inspect or edit display metadata embedded in compiled Hollyhock .bin/.hhk files."""

from __future__ import annotations

import argparse
import os
import shutil
import stat
import struct
import sys
import tempfile
from pathlib import Path

ATTRS = ("name", "description", "author", "version")
SECTIONS = {
    "name": ".hollyhock_name",
    "description": ".hollyhock_description",
    "author": ".hollyhock_author",
    "version": ".hollyhock_version",
}
ELF_HEADER_SIZE = 52
ELF_SHDR_SIZE = 40
ELF_PHDR_SIZE = 32
SHT_NULL, SHT_PROGBITS, SHT_STRTAB, SHT_NOBITS = 0, 1, 3, 8
SHF_ALLOC, PT_LOAD = 2, 1


class MetadataError(Exception):
    pass


def align(value: int, boundary: int) -> int:
    return ((value + boundary - 1) // boundary) * boundary if boundary > 1 else value


def encode_value(name: str, value: str, allow_empty: bool) -> bytes:
    if not allow_empty and not value:
        raise MetadataError(f"{name}: empty values are unsupported in .bin files.")
    try:
        result = value.encode("ascii")
    except UnicodeEncodeError as exc:
        raise MetadataError(f"{name}: printable ASCII only (0x20-0x7E) is supported.") from exc
    if any(byte < 0x20 or byte > 0x7E for byte in result):
        raise MetadataError(f"{name}: printable ASCII only (0x20-0x7E) is supported.")
    return result


def decode_value(name: str, raw: bytes) -> str:
    try:
        value = raw.decode("ascii")
    except UnicodeDecodeError as exc:
        raise MetadataError(f"{name}: metadata is not ASCII; refusing to edit.") from exc
    if any(ord(ch) < 0x20 or ord(ch) > 0x7E for ch in value):
        raise MetadataError(f"{name}: metadata contains non-printable bytes.")
    return value


def parse_bin(data: bytes) -> dict:
    if len(data) < 0x14:
        raise MetadataError("File is too small to be a Hollyhock .bin.")
    entry = struct.unpack_from(">I", data, 8)[0]
    base = struct.unpack_from(">I", data, 12)[0]
    if entry >> 24 != 0x8C or base >> 24 != 0x8C or entry < base:
        raise MetadataError("Unexpected .bin header; expected SH-4 0x8Cxxxxxx addresses.")
    entry_offset = entry - base
    if entry_offset >= len(data):
        raise MetadataError("The entry point lies outside the file.")

    fields = {}
    cursor = 0x10
    for index, name in enumerate(ATTRS):
        start = cursor
        nul = data.find(b"\0", start)
        if nul < 0 or nul == start:
            raise MetadataError(
                f".bin {name} field is empty or unterminated. The current launcher "
                "expects four non-empty strings: name, description, author, version."
            )
        value = decode_value(name, data[start:nul])

        if index < len(ATTRS) - 1:
            # The legacy launcher skips all zero padding before the next field.
            # Find that next printable field start so a previously shortened
            # value can still retain its original reserved slot capacity.
            slot_end = nul + 1
            while slot_end < entry_offset and data[slot_end] == 0:
                slot_end += 1
            if slot_end >= entry_offset:
                raise MetadataError(f".bin {name} has no following metadata field.")
            if slot_end & 3:
                raise MetadataError(f".bin {name} next field is not 4-byte aligned.")
        else:
            # The version field is followed by the BIN's 4-byte alignment
            # padding. The executable stub may begin before e_entry, so do
            # not use entry_offset as the metadata boundary.
            slot_end = align(nul + 1, 4)
            if slot_end <= nul or any(data[nul + 1:slot_end]):
                raise MetadataError(".bin version alignment padding is not zero-filled.")

        if slot_end > len(data) or slot_end > entry_offset:
            raise MetadataError(f".bin {name} metadata overlaps the program entry point.")
        fields[name] = {"value": value, "start": start, "end": slot_end,
                        "capacity": slot_end - start}
        cursor = slot_end
    if cursor > entry_offset:
        raise MetadataError(".bin metadata overlaps the declared entry point.")
    return {"format": "BIN", "fields": fields, "entry": entry,
            "base": base, "entry_offset": entry_offset}


def patch_bin(data: bytes, meta: dict, updates: dict) -> bytes:
    out = bytearray(data)
    for name, value in updates.items():
        raw = encode_value(name, value, allow_empty=False)
        info = meta["fields"][name]
        capacity = info["capacity"]
        if len(raw) + 1 > capacity:
            raise MetadataError(
                f"{name}: {len(raw)} characters do not fit (maximum {capacity - 1}). "
                "Rebuild the application with a longer metadata string to reserve more space."
            )
        # The launcher treats zero bytes as the field terminator and skips
        # zero padding until the next field. Preserve the complete slot using
        # zero fill, so existing launcher versions display no trailing spaces.
        out[info["start"]:info["end"]] = raw + bytes(capacity - len(raw))
    return bytes(out)


def parse_elf(data: bytes) -> dict:
    if len(data) < ELF_HEADER_SIZE or data[:4] != b"\x7fELF":
        raise MetadataError("File does not contain a complete ELF header.")
    if data[4:7] != bytes((1, 2, 1)):
        raise MetadataError("Expected big-endian ELF32.")
    if data[7] != 0:
        raise MetadataError("Expected the System-V ELF ABI used by the launcher.")

    (etype, machine, version, entry, phoff, shoff, _flags, ehsize, phentsize,
     phnum, shentsize, shnum, shstrndx) = struct.unpack_from(">HHIIIIIHHHHHH", data, 16)
    if (etype, machine, version) != (2, 42, 1):
        raise MetadataError("Expected a static SH ELF executable (ET_EXEC).")
    if ehsize != ELF_HEADER_SIZE or shentsize != ELF_SHDR_SIZE or not shnum:
        raise MetadataError("Unsupported or missing ELF section header table.")
    if shstrndx >= shnum or shoff > len(data) or shnum > (len(data) - shoff) // shentsize:
        raise MetadataError("Invalid ELF section header table.")

    phdrs = []
    if phnum:
        if phentsize != ELF_PHDR_SIZE or phoff > len(data) or phnum > (len(data) - phoff) // phentsize:
            raise MetadataError("Invalid ELF program header table.")
        for i in range(phnum):
            ph = struct.unpack_from(">IIIIIIII", data, phoff + i * phentsize)
            if ph[1] > len(data) or ph[4] > len(data) - ph[1]:
                raise MetadataError(f"ELF program header {i} extends beyond the file.")
            phdrs.append(ph)

    shdrs = []
    for i in range(shnum):
        sh = list(struct.unpack_from(">IIIIIIIIII", data, shoff + i * shentsize))
        if sh[1] not in (SHT_NULL, SHT_NOBITS) and (sh[4] > len(data) or sh[5] > len(data) - sh[4]):
            raise MetadataError(f"ELF section {i} extends beyond the file.")
        shdrs.append(sh)

    names_sh = shdrs[shstrndx]
    if names_sh[1] != SHT_STRTAB:
        raise MetadataError("ELF section-name table is not a string table.")
    names_data = data[names_sh[4]:names_sh[4] + names_sh[5]]
    names = []
    for i, sh in enumerate(shdrs):
        offset = sh[0]
        if offset >= len(names_data):
            if i == 0 and offset == 0:
                names.append("")
                continue
            raise MetadataError(f"ELF section {i} has an invalid name offset.")
        end = names_data.find(b"\0", offset)
        if end < 0:
            raise MetadataError(f"ELF section {i} name is not terminated.")
        try:
            names.append(names_data[offset:end].decode("ascii"))
        except UnicodeDecodeError as exc:
            raise MetadataError("ELF section names are not ASCII.") from exc

    fields = {}
    for name, section_name in SECTIONS.items():
        matches = [i for i, candidate in enumerate(names) if candidate == section_name]
        if len(matches) > 1:
            raise MetadataError(f"Duplicate {section_name} sections.")
        if not matches:
            continue
        index = matches[0]
        sh = shdrs[index]
        if sh[1] != SHT_PROGBITS or sh[2] & SHF_ALLOC:
            raise MetadataError(f"{section_name} is not a non-allocatable PROGBITS section.")
        if any(ph[0] == PT_LOAD and sh[5] and sh[4] < ph[1] + ph[4]
               and sh[4] + sh[5] > ph[1] for ph in phdrs):
            raise MetadataError(f"{section_name} overlaps a PT_LOAD segment.")

        next_addresses = [other[3] for i, other in enumerate(shdrs)
                          if i != index and other[1] != SHT_NULL and other[3] > sh[3]]
        capacity = sh[5] if sh[3] == 0 or not next_addresses else min(next_addresses) - sh[3]
        if capacity <= 0 or capacity < sh[5]:
            raise MetadataError(f"{section_name} has an inconsistent address range.")
        raw = data[sh[4]:sh[4] + sh[5]]
        nul = raw.find(b"\0")
        value = decode_value(name, raw if nul < 0 else raw[:nul])
        fields[name] = {"value": value, "index": index, "capacity": capacity}

    return {"format": "HHK", "fields": fields, "shdrs": shdrs,
            "entry": entry, "shnum": shnum}


def patch_elf(data: bytes, meta: dict, updates: dict) -> bytes:
    out = bytearray(data)
    shdrs = [list(sh) for sh in meta["shdrs"]]
    for name, value in updates.items():
        if name not in meta["fields"]:
            raise MetadataError(f"This .hhk file has no {SECTIONS[name]} section.")
        raw = encode_value(name, value, allow_empty=True)
        info = meta["fields"][name]
        capacity = info["capacity"]
        if len(raw) + 1 > capacity:
            raise MetadataError(
                f"{name}: {len(raw)} characters do not fit (maximum {capacity - 1}). "
                "Rebuild the application with a longer metadata string to reserve more space."
            )
        # Metadata sections are non-allocatable. Append replacement data and
        # repoint the section; executable data and all load addresses stay put.
        # Zero padding keeps the launcher from rendering trailing spaces.
        payload = raw + bytes(capacity - len(raw))
        sh = shdrs[info["index"]]
        offset = align(len(out), max(1, sh[8]))
        out.extend(bytes(offset - len(out)))
        out.extend(payload)
        sh[4], sh[5] = offset, len(payload)

    table_offset = align(len(out), 4)
    out.extend(bytes(table_offset - len(out)))
    for sh in shdrs:
        out.extend(struct.pack(">IIIIIIIIII", *sh))
    struct.pack_into(">I", out, 32, table_offset)
    return bytes(out)


def load_metadata(data: bytes) -> dict:
    return parse_elf(data) if data.startswith(b"\x7fELF") else parse_bin(data)


def show(meta: dict) -> None:
    print(f"Format: {meta['format']}")
    if meta["format"] == "BIN":
        print(f"Load address:  0x{meta['base']:08X}")
        print(f"Entry address: 0x{meta['entry']:08X}")
        print(f"Entry offset:  0x{meta['entry_offset']:X}")
    else:
        print(f"Entry address: 0x{meta['entry']:08X}")
    for name in ATTRS:
        info = meta["fields"].get(name)
        if info is None:
            print(f"{name:11}: <not present>")
        else:
            print(f"{name:11}: {info['value']!r} (in-place maximum: {info['capacity'] - 1} characters)")


def write_result(source: Path, result: bytes, args: argparse.Namespace) -> Path:
    if args.in_place:
        target = source
        backup = Path(str(source) + ".bak")
        if backup.exists():
            raise MetadataError(f"Backup already exists: {backup}. Move it away before retrying.")
        shutil.copy2(source, backup)
    elif args.output:
        target = Path(args.output).expanduser().resolve()
    else:
        target = source.with_name(f"{source.stem}.patched{source.suffix}")

    if target.resolve() == source.resolve() and not args.in_place:
        raise MetadataError("Output equals input; use --in-place explicitly.")
    if target.exists() and not (args.force or args.in_place):
        raise MetadataError(f"Output already exists: {target}. Use --force to overwrite.")
    target.parent.mkdir(parents=True, exist_ok=True)

    fd, temporary = tempfile.mkstemp(prefix=f".{target.name}.", dir=str(target.parent))
    try:
        with os.fdopen(fd, "wb") as stream:
            stream.write(result)
            stream.flush()
            os.fsync(stream.fileno())
        os.chmod(temporary, stat.S_IMODE(source.stat().st_mode))
        os.replace(temporary, target)
    except Exception:
        try:
            os.unlink(temporary)
        except OSError:
            pass
        raise
    return target


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Inspect or edit metadata embedded in compiled Hollyhock .bin/.hhk files."
    )
    commands = parser.add_subparsers(dest="command", required=True)
    p_show = commands.add_parser("show", help="show metadata and in-place capacity")
    p_show.add_argument("file")
    p_set = commands.add_parser("set", help="edit one or more metadata attributes")
    p_set.add_argument("file")
    for name in ATTRS:
        p_set.add_argument(f"--{name}", help=f"new {name}")
    destination = p_set.add_mutually_exclusive_group()
    destination.add_argument("-o", "--output", help="write to a new file at this path")
    destination.add_argument("--in-place", action="store_true", help="edit input; create FILE.bak first")
    p_set.add_argument("--force", action="store_true", help="overwrite an existing output file")

    args = parser.parse_args()
    source = Path(args.file).expanduser().resolve()
    try:
        data = source.read_bytes()
        meta = load_metadata(data)
        if args.command == "show":
            show(meta)
            return 0
        updates = {name: getattr(args, name) for name in ATTRS if getattr(args, name) is not None}
        if not updates:
            raise MetadataError("Specify at least one of --name, --description, --author, or --version.")
        result = patch_bin(data, meta, updates) if meta["format"] == "BIN" else patch_elf(data, meta, updates)
        target = write_result(source, result, args)
        print(f"Wrote: {target}")
        if args.in_place:
            print(f"Backup: {target}.bak")
        for name, value in updates.items():
            print(f"  {name}: {value}")
        return 0
    except (OSError, MetadataError, struct.error) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
