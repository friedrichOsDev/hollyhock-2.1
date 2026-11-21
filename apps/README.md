# Applications

This directory collects standalone applications and projects developed for Hollyhock 2.1 and the Casio fx-CP400 family. Each app is maintained as its own project and may have its own source code, build instructions, dependencies, and release artifacts.

## Applications

| Application | Directory | Description | Status |
|---|---|---|---|
| **CPV Player** | `cpv-player/` | A player application for CPV files. | In development |

More applications will be listed here as they are added. This index is intended to grow over time.

## Directory layout

Each application should live in its own subdirectory:

```text
apps/
├── README.md
└── <app-name>/
    ├── README.md       # Overview, requirements, build and usage instructions
    ├── build.sh        # Build script for the app
    ├── Makefile        # Makefile for the app
    ├── linker_*.ld     # Linker scripts for the app (bin linker and hhk linker)
    ├── main.cpp        # Main source file for the app
    ├── start.s         # Hollyhock startup code
    └── ...             # App-specific files
```

This is a suggested layout, not a requirement. Small projects may use a simpler structure, and projects with different languages or toolchains can organize their files as needed.

## Project conventions

- Keep each app self-contained in its own directory.
- Give each app a README explaining what it does, supported hardware/software, how to build it, and how to install or run it.
- Keep app-specific build instructions and dependencies in that app's directory.
- Avoid committing generated binaries or build output unless they are intentionally distributed as part of the project.
- Update this index when adding an application.

## Related

- [Hollyhock 2.1 repository README](../README.md)
- [Developing Hollyhock applications](../doc/user/developing.md)
- [Hollyhock UI Library](../doc/ui-library/README.md)
