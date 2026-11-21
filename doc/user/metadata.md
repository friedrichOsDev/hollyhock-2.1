# Editing compiled program metadata

The Hollyhock Launcher can display metadata embedded in the compiled program.
This is separate from the filename stored inside some applications. There are
therefore two different things that may need changing:

- **Launcher metadata** (display name, description, author, version): use
  \`tools/hhkmeta.py\`.
- **An application's hard-coded calculator file path** (for example
  \`\\fls0\\PythonExtra-hh2.bin\`): use \`tools/hhkrename.py\`.

Renaming a file in the computer's file manager does not update either value
inside the binary.

## Inspect metadata

From the repository root:

\`\`\`sh
python3 tools/hhkmeta.py show path/to/application.bin
python3 tools/hhkmeta.py show path/to/application.hhk
\`\`\`

The output shows the compiled name, description, author and version, along
with the maximum number of characters currently available in each field.

## Change launcher metadata

Use \`set\` to edit one or more metadata attributes. By default, the tool
creates a new file named \`<original-name>.patched.<extension>\`:

\`\`\`sh
python3 tools/hhkmeta.py set path/to/application.bin \\
    --name "New Display Name" \\
    --author "New Author"
\`\`\`

Choose a specific output path with \`-o\`:

\`\`\`sh
python3 tools/hhkmeta.py set path/to/application.hhk \\
    --name "My Application" \\
    --description "A short description" \\
    -o path/to/MyApplication.hhk
\`\`\`

To edit the input file directly, use \`--in-place\`. The original is saved as
\`FILE.bak\` first:

\`\`\`sh
python3 tools/hhkmeta.py set path/to/application.bin \\
    --name "New Display Name" --in-place
\`\`\`

## Rename a file and update its embedded calculator path

Some applications contain an absolute calculator path in their program image,
such as \`\\fls0\\PythonExtra-hh2.bin\`. The application opens that exact path at
runtime. Renaming the file on the computer or calculator alone leaves the
embedded string unchanged and can make the application fail to start.

Use the separate \`tools/hhkrename.py\` utility for this case:

\`\`\`sh
python3 tools/hhkrename.py PythonExtra-hh2.bin PythonExtra.bin
\`\`\`

This performs two linked operations:

1. Replaces the embedded path with \`\\fls0\\PythonExtra.bin\` (or preserves
   \`\\drv0\\\` when that is the path found in the program).
2. Writes the file under the new name and removes the old source file only
   after the updated destination has been written successfully.

Run it from the folder containing the file, or pass full paths to both
arguments. Use \`--dry-run\` to inspect the embedded path changes without
writing anything:

\`\`\`sh
python3 tools/hhkrename.py PythonExtra-hh2.bin PythonExtra.bin --dry-run
\`\`\`

**This utility does not change the launcher's display metadata.** Use
\`hhkmeta.py\` separately only when the displayed name or other metadata also
needs changing.

## Safety and format limits

- \`hhkrename.py\` only changes a full embedded \`\\fls0\\...\` or \`\\drv0\\...\`
  path whose basename exactly matches the source filename. It updates every
  matching occurrence and refuses to guess when no match is found.
- It uses only the NUL bytes already following the embedded string as
  additional path capacity. If the new path is too long, it stops before
  changing the source file.
- It never overwrites an existing destination. If the original cannot be
  removed after the new file is written, both copies are kept and a warning is
  printed.
- \`hhkmeta.py\` edits only the launcher's metadata fields. It does not rename
  files or change embedded file paths.
- Metadata values supported by the launcher are printable ASCII
  (characters \`0x20\` through \`0x7E\`).
- For \`.bin\`, all four metadata fields must be present and non-empty because
  the current launcher parser skips empty fields. For \`.hhk\`, an attribute
  can only be changed if its \`.hollyhock_*\` section exists.

Keep a backup of the original application until the renamed program has been
tested on the calculator.
