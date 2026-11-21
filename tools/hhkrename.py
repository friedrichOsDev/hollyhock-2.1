#!/usr/bin/env python3
"""Rename compiled Hollyhock files and update recognizable embedded references.

Only printable, NUL-terminated ASCII strings are rewritten. A filename that
appears as a standalone string or as the basename of a path is updated when
the replacement fits the string's existing zero-padded storage. If no such
reference is found, the utility performs a normal file rename.
"""
from __future__ import annotations

import argparse
import os
import stat
import sys
from pathlib import Path
from typing import List, Tuple


class RenameError(Exception):
    """Raised when the requested rename cannot be completed safely."""


def _valid_name(name: str) -> bytes:
    try:
        raw = name.encode("ascii")
    except UnicodeEncodeError as exc:
        raise RenameError("Filenames must use ASCII characters only.") from exc
    if not raw or any(b < 0x20 or b > 0x7E for b in raw) or b"/" in raw or b"\\" in raw:
        raise RenameError("Pass a printable ASCII basename, not a path.")
    return raw


def _c_string_ranges(data: bytes) -> List[Tuple[int, int, int]]:
    """Return (start, end-exclusive, slot-end) for printable ASCII C strings."""
    ranges = []
    i = 0
    while i < len(data):
        if data[i] < 0x20 or data[i] > 0x7E:
            i += 1
            continue
        start = i
        while i < len(data) and 0x20 <= data[i] <= 0x7E:
            i += 1
        end = i
        if i < len(data) and data[i] == 0:
            slot_end = i + 1
            while slot_end < len(data) and data[slot_end] == 0:
                slot_end += 1
            ranges.append((start, end, slot_end))
    return ranges


def find_references(data: bytes, old_name: bytes):
    """Find basename references in printable NUL-terminated strings."""
    findings = []
    for start, end, slot_end in _c_string_ranges(data):
        string = data[start:end]
        pos = 0
        while True:
            index = string.find(old_name, pos)
            if index < 0:
                break
            after = index + len(old_name)
            before_ok = index == 0 or string[index - 1] in (ord("/"), ord("\\"))
            after_ok = after == len(string) or string[after] in (ord("/"), ord("\\"))
            if before_ok and after_ok:
                findings.append({
                    "offset": start + index,
                    "string_start": start,
                    "string_end": end,
                    "slot_end": slot_end,
                    "old_string": string,
                    "name_offset": index,
                    "kind": "path" if b"/" in string or b"\\" in string else "filename",
                })
            pos = after
    return findings


def patch_references(data: bytes, findings, new_name: bytes):
    out = bytearray(data)
    changes = []
    grouped = {}
    for finding in findings:
        key = (finding["string_start"], finding["string_end"], finding["slot_end"])
        grouped.setdefault(key, []).append(finding)

    for (start, end, slot_end), group in grouped.items():
        original = bytes(out[start:end])
        updated = original
        old_name_len = len(group[0]["old_string"])  # replaced below by basename length
        # The exact matched token length is retained in the finding to avoid
        # accidentally using the whole surrounding path length.
        old_token_len = group[0]["token_length"]
        for pos in sorted((f["name_offset"] for f in group), reverse=True):
            updated = updated[:pos] + new_name + updated[pos + old_token_len:]
        capacity = slot_end - start
        if len(updated) + 1 > capacity:
            raise RenameError(
                f"String at 0x{start:X} needs {len(updated)+1} bytes including NUL, "
                f"but its existing zero-padded slot has only {capacity}. "
                "Choose a shorter name or rebuild the program. No files were changed."
            )
        out[start:slot_end] = updated + bytes(capacity - len(updated))
        changes.append((start, original, updated, capacity))
    return bytes(out), changes


def write_new(destination: Path, data: bytes, mode: int):
    if destination.exists():
        raise RenameError(f"Destination already exists: {destination}")
    if not destination.parent.is_dir():
        raise RenameError(f"Destination directory does not exist: {destination.parent}")
    created = False
    try:
        with destination.open("xb") as stream:
            created = True
            stream.write(data)
            stream.flush()
            os.fsync(stream.fileno())
        os.chmod(destination, stat.S_IMODE(mode))
    except Exception:
        if created:
            try:
                destination.unlink()
            except OSError:
                pass
        raise


def build_parser():
    parser = argparse.ArgumentParser(
        description="Rename .bin/.hhk files and update recognizable embedded filename/path strings when present."
    )
    parser.add_argument("source", help="existing compiled .bin or .hhk file")
    parser.add_argument("destination", help="new basename; relative paths are placed beside source")
    parser.add_argument("--dry-run", action="store_true", help="analyze and report without writing")
    return parser


def main(argv=None):
    args = build_parser().parse_args(argv)
    source = Path(args.source).expanduser().resolve()
    dest_arg = Path(args.destination).expanduser()
    destination = (source.parent / dest_arg).resolve() if not dest_arg.is_absolute() else dest_arg.resolve()
    try:
        old_name = _valid_name(source.name)
        new_name = _valid_name(destination.name)
        if not source.is_file():
            raise RenameError(f"Source is not a regular file: {source}")
        if source == destination:
            raise RenameError("Source and destination are identical.")
        if source.suffix.lower() not in (".bin", ".hhk"):
            raise RenameError("Only .bin and .hhk files are supported.")
        if destination.suffix.lower() != source.suffix.lower():
            raise RenameError("Keep the original extension (.bin or .hhk).")
        if destination.exists():
            raise RenameError(f"Destination already exists: {destination}")

        data = source.read_bytes()
        findings = find_references(data, old_name)
        patched, changes = patch_references(data, findings, new_name) if findings else (data, [])
        for start, old_string, new_string, capacity in changes:
            kind = "path" if b"/" in old_string or b"\\" in old_string else "filename"
            print(
                f"0x{start:08X}: {kind}: {old_string.decode('ascii')} -> "
                f"{new_string.decode('ascii')} (slot {capacity} bytes)"
            )
        if not findings:
            print(f"No recognizable references to {source.name} found; file contents will remain unchanged.")
        else:
            print(f"Updated {len(findings)} reference(s) in {len(changes)} string(s).")
        if args.dry_run:
            print(f"Dry run: would rename {source} -> {destination}; no files changed.")
            return 0

        write_new(destination, patched, source.stat().st_mode)
        try:
            source.unlink()
        except OSError as exc:
            print(
                f"warning: wrote {destination}, but could not remove original {source}: "
                f"{exc}. Both files remain.",
                file=sys.stderr,
            )
            return 1
        print(f"Renamed: {source} -> {destination}")
        if findings:
            print(f"Patched {len(findings)} embedded filename/path reference(s).")
        else:
            print("Renamed only; no embedded references were modified.")
        return 0
    except (OSError, RenameError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
