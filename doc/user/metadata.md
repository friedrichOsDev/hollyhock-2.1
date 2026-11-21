# Editing compiled program metadata

The name shown by the Hollyhock Launcher is stored inside the compiled program.
It is not taken from the `.bin` or `.hhk` filename when a display name is
present. Renaming `PythonExtra.bin` to `Python.bin`, for example, does not
change the launcher's displayed name.

Use `tools/hhkmeta.py` to inspect or change the metadata of an already compiled
`.bin` or `.hhk` file. You do not need to rebuild or relink the application for
an edit that fits in the space already reserved for that attribute. The tool
uses Python 3 from the standard library; no SH-4 compiler or third-party
package is required.

## Inspect metadata

From the repository root, run:

```sh
python3 tools/hhkmeta.py show path/to/application.bin
python3 tools/hhkmeta.py show path/to/application.hhk
```

The output shows the compiled name, description, author and version, along
with the maximum number of characters that can currently fit in each field.

## Change metadata

Specify one or more attributes. The tool creates a new file named
`<original-name>.patched.<extension>` by default:

```sh
python3 tools/hhkmeta.py set path/to/PythonExtra.bin \
    --name "Python Extra" \
    --author "Python Extra contributors" \
    --version "1.0.0"
```

To choose a different output path:

```sh
python3 tools/hhkmeta.py set path/to/application.hhk \
    --name "My Application" \
    --description "A short description" \
    -o path/to/MyApplication.hhk
```

To update the input file directly, use `--in-place`. The original is saved as
`FILE.bak` first. The tool refuses to overwrite an existing backup:

```sh
python3 tools/hhkmeta.py set path/to/application.bin \
    --name "My Application" --in-place
```

Copy the resulting `.bin` or `.hhk` file to the calculator as usual. The
filename can be changed independently if desired; the metadata controls the
name and other information displayed by the launcher.

## Rename the file and embedded program name together

Use the `rename` command when the filename and the metadata name must match.
The destination filename (without its extension) becomes the embedded launcher
name. The command supports both `.bin` and `.hhk` files:

```sh
python3 tools/hhkmeta.py rename PythonExtra-hh2.bin PythonExtra.bin
```

For the example above, the tool changes the embedded name to `PythonExtra`,
writes `PythonExtra.bin`, and removes the old `PythonExtra-hh2.bin` path only
after the new file has been written successfully. The description, author,
version, load address, entry point, and executable code are preserved.

The destination must use the same format extension as the source. An existing
destination is never overwritten. If the new display name does not fit in the
compiled metadata slot, the command fails and leaves the original file
untouched. If source deletion fails after the new file is written, the tool
reports the situation and keeps both files.

## Important limits

- The tool preserves the program's load address, entry point and executable
  bytes. It only writes into the metadata space already reserved by the
  compiled file.
- If an attribute is longer than its available slot, the tool stops with an
  error and reports the current maximum. Rebuild the application with a longer
  metadata string to reserve more space; do not try to force the edit.
- `.bin` metadata uses the launcher's existing four-field format. All four
  fields must be present and non-empty because the current BIN metadata parser
  skips empty fields.
- A `.hhk` attribute can only be changed when its corresponding
  `.hollyhock_*` ELF section exists in the file.
- Metadata values are limited to printable ASCII (characters `0x20` through
  `0x7E`). This matches the launcher's current text display support.
- Unused bytes in fixed metadata slots are zero-padded, matching the
  launcher's string parser and avoiding visible trailing spaces. The first
  three BIN slots retain their detected boundaries because the next metadata
  string marks their end. The final BIN version slot ends at its 4-byte
  alignment boundary; after shortening the version, the reported maximum for
  that field can therefore shrink. To make a later, longer version change,
  start again from the untouched original BIN or rebuild the application.

Always keep a copy of the original application until you have tested the
patched file on the calculator.
