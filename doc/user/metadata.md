# Editing compiled program metadata and renaming binaries

Compiled programs may contain different kinds of names:

- **Launcher metadata** (display name, description, author, version): use
  `tools/hhkmeta.py`.
- **Embedded references to the physical filename**: use
  `tools/hhkrename.py`. This searches recognizable strings and updates matching
  references when found; otherwise it simply renames the file.

Renaming in a file manager does not change launcher metadata or embedded strings.

## Inspect metadata

From the repository root:

```sh
python3 tools/hhkmeta.py show path/to/application.bin
python3 tools/hhkmeta.py show path/to/application.hhk
```

## Change launcher metadata

Use `set` to edit one or more attributes. By default, the tool creates
`<original-name>.patched.<extension>`:

```sh
python3 tools/hhkmeta.py set application.bin --name "New Display Name" --author "New Author"
```

Choose a specific output path with `-o`:

```sh
python3 tools/hhkmeta.py set application.hhk \
    --name "My Application" --description "A short description" \
    -o MyApplication.hhk
```

To edit the input directly, use `--in-place`. A `FILE.bak` backup is created
first:

```sh
python3 tools/hhkmeta.py set application.bin --name "New Display Name" --in-place
```

## Rename a compiled file and patch references

Some programs embed their own filename or a path to it, such as
`\\fls0\\PythonExtra-hh2.bin`. When a program expects that exact path,
changing only the filesystem filename can break it.

Use:

```sh
python3 tools/hhkrename.py PythonExtra-hh2.bin PythonExtra.bin
```

The rename utility:

1. Scans the binary for printable ASCII, NUL-terminated strings containing the
   source basename as a complete token (standalone or after/before a slash or
   backslash, such as in a path).
2. Updates every matching occurrence to the destination basename.
3. Uses existing zero padding after the string as extra capacity. If the
   replacement does not fit, it aborts without changing either file.
4. Writes the destination first and removes the source only after the new file
   has been written successfully. It refuses to overwrite an existing target.

If no recognizable references are found, it performs an ordinary file rename
and leaves the binary contents unchanged. Use `--dry-run` to inspect the
analysis without writing:

```sh
python3 tools/hhkrename.py PythonExtra-hh2.bin PythonExtra.bin --dry-run
```

This tool does not change launcher metadata. The display name remains whatever
was compiled into the metadata; edit it separately with `hhkmeta.py` if desired.

## Limits and safety

- The scanner recognizes printable ASCII strings terminated by NUL and matches
  the filename case-insensitively. It does not currently decode UTF-16,
  compressed/encrypted data, or runtime-built strings, and it cannot prove that
  a program has no other filename dependency.
- A match is changed only when it appears as a complete filename token,
  avoiding incidental matches inside longer words.
- Replacement strings must be ASCII basenames. Directory components are not
  accepted as the destination name.
- Existing NUL padding is the only spare storage used. The utility never moves
  code or expands the string's allocated area.
- Existing destination files are not overwritten. If writing succeeds but the
  source cannot be removed, both files are left in place and a warning is
  printed.
- `hhkmeta.py` edits only launcher metadata. Values must be printable ASCII
  (0x20-0x7E). For BIN files, all four fields must be present and non-empty.
  For HHK files, a field can only be edited if its `.hollyhock_*` section exists.

Keep a backup until the renamed program has been tested on the calculator.
