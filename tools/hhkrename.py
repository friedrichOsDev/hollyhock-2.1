#!/usr/bin/env python3
"""Rename a Hollyhock program file and update its embedded calculator path.

This tool changes the hard-coded path (for example, \\fls0\\OldName.bin)
inside the program image. It deliberately does not edit launcher metadata.
"""

from __future__ import annotations

import argparse
import os
import re
import stat
import sys
from pathlib import Path
from typing import List, Optional, Tuple


class RenameError(Exception):
    """Raised when a safe embedded-path update is not possible."""


# This is the path form found in compiled Hollyhock applications. Keep this
# intentionally narrow: never guess at arbitrary strings that merely resemble
# a filename.
EMBEDDED_PATH_RE = re.compile(
    rb"\\(?:fls0|drv0)\\[^\x00\r\n]{1,240}\.(?:bin|hhk)\x00",
    re.IGNORECASE,
)


def find_path_references(
    data: bytes, source_name: str
) -> List[Tuple[int, bytes, int]]:
    """Return (offset, old_path_without_NUL, writable_zero_slot_size)."""
    try:
        source_ascii = source_name.encode("ascii")
    except UnicodeEncodeError as exc:
        raise RenameError("The source filename must contain ASCII characters only.") from exc

    references: List[Tuple[int, bytes, int]] = []
    for match in EMBEDDED_PATH_RE.finditer(data):
        old_path = match.group(0)[:-1]
        embedded_name = old_path.rsplit(b"\\", 1)[-1]
        if embedded_name != source_ascii:
            continue

        start = match.start()
        slot_end = match.end()
        # Only use bytes that are already zero immediately after the C-string.
        # This safely handles the reserved static character buffer seen in
        # PythonExtra (0x106..0x12F), without moving or overwriting program data.
        while slot_end < len(data) and data[slot_end] == 0:
            slot_end += 1
        references.append((start, old_path, slot_end - start))

    return references


def patch_embedded_paths(
    data: bytes,
    references: List[Tuple[int, bytes, int]],
    destination_name: str,
) -> Tuple[bytes, List[Tuple[int, bytes, bytes, int]]]:
    try:
        new_name = destination_name.encode("ascii")
    except UnicodeEncodeError as exc:
        raise RenameError("The destination filename must contain ASCII characters only.") from exc
    if not new_name or any(byte < 0x20 or byte > 0x7E for byte in new_name):
        raise RenameError("The destination filename must be printable ASCII.")
    if b"\\" in new_name or b"/" in new_name:
        raise RenameError("Pass a filename, not a directory path, as the destination basename.")

    output = bytearray(data)
    changes: List[Tuple[int, bytes, bytes, int]] = []
    for start, old_path, capacity in references:
        volume_prefix = old_path.rsplit(b"\\", 1)[0] + b"\\"
        new_path = volume_prefix + new_name
        encoded = new_path + b"\0"
        if len(encoded) > capacity:
            maximum = capacity - len(volume_prefix) - 1
            raise RenameError(
                f"Embedded path at offset 0x{start:X} has room for at most "
                f"{maximum} filename bytes; '{destination_name}' needs "
                f"{len(new_name)}. Choose a shorter filename or rebuild with "
                "a larger path buffer. The original file was not changed."
            )
        output[start:start + capacity] = encoded + bytes(capacity - len(encoded))
        changes.append((start, old_path, new_path, capacity))

    return bytes(output), changes


def write_destination(destination: Path, data: bytes, source_mode: int) -> None:
    if destination.exists():
        raise RenameError(f"Destination already exists: {destination}")
    if not destination.parent.is_dir():
        raise RenameError(f"Destination directory does not exist: {destination.parent}")

    created = False
    try:
        # Exclusive creation avoids overwriting files and works on FAT/exFAT
        # calculator storage, where hard links are not generally supported.
        with destination.open("xb") as stream:
            created = True
            stream.write(data)
            stream.flush()
            os.fsync(stream.fileno())
        os.chmod(destination, stat.S_IMODE(source_mode))
    except Exception:
        if created:
            try:
                destination.unlink()
            except OSError:
                pass
        raise


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description=(
            "Rename a compiled .bin/.hhk file and update its embedded "
            r"\fls0\ or \drv0\ filename reference. Does not edit launcher metadata."
        )
    )
    parser.add_argument("source", help="existing compiled .bin or .hhk file")
    parser.add_argument(
        "destination",
        help="new filename; a relative filename is placed beside the source",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="show embedded path changes without writing or renaming",
    )
    return parser


def main(argv: Optional[List[str]] = None) -> int:
    args = build_parser().parse_args(argv)
    source = Path(args.source).expanduser().resolve()
    destination_arg = Path(args.destination).expanduser()
    destination = (
        (source.parent / destination_arg).resolve()
        if not destination_arg.is_absolute()
        else destination_arg.resolve()
    )

    try:
        if not source.is_file():
            raise RenameError(f"Source file does not exist or is not a regular file: {source}")
        if source == destination:
            raise RenameError("Source and destination are identical.")
        if source.suffix.lower() not in (".bin", ".hhk"):
            raise RenameError("Only compiled .bin and .hhk files are supported.")
        if destination.suffix.lower() != source.suffix.lower():
            raise RenameError("Keep the same file extension (.bin or .hhk).")
        if destination.exists():
            raise RenameError(f"Destination already exists: {destination}")

        data = source.read_bytes()
        references = find_path_references(data, source.name)
        if not references:
            candidates = [
                match.group(0)[:-1].decode("ascii", "replace")
                for match in EMBEDDED_PATH_RE.finditer(data)
            ]
            found = ""
            if candidates:
                found = " Embedded paths found: " + ", ".join(repr(x) for x in candidates[:8])
            raise RenameError(
                f"No embedded calculator path ending in '{source.name}' was found."
                " Refusing to guess which other path should be changed." + found
            )

        patched, changes = patch_embedded_paths(data, references, destination.name)
        # Verify the intended path values before any disk operation.
        for start, old_path, new_path, capacity in changes:
            if patched.count(new_path + b"\0") < 1:
                raise RenameError(f"Internal verification failed for path at 0x{start:X}.")
            print(
                f"0x{start:08X}: {old_path.decode('ascii')} -> "
                f"{new_path.decode('ascii')} (slot: {capacity} bytes)"
            )

        if args.dry_run:
            print("Dry run only; no files changed.")
            return 0

        write_destination(destination, patched, source.stat().st_mode)
        try:
            source.unlink()
        except OSError as exc:
            print(
                f"warning: wrote {destination}, but could not remove original "
                f"{source}: {exc}\nBoth files have been kept.",
                file=sys.stderr,
            )
            return 1

        print(f"Renamed: {source} -> {destination}")
        print(f"Updated {len(changes)} embedded path reference(s).")
        print("Launcher metadata (name, description, author, version) was not changed.")
        return 0
    except (OSError, RenameError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
