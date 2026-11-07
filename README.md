# OpenImage

OpenImage is in development. This build operates on regular files, including offline raw disk-image files. It can back up, restore, clone, verify, compare, and inspect OMI headers. See the [release blockers](docs/RELEASE-BLOCKERS.md) before relying on this build for recovery.

    openimage backup offline.raw backup.omi zstd
    openimage verify backup.omi
    openimage restore backup.omi restored.raw
    openimage compare backup.omi restored.raw
    openimage clone offline.raw cloned.raw
    openimage explore backup.omi

Keep inputs offline and stable throughout each operation. Use a trusted local output directory and choose a new filename. Use `openimage --tui` in an interactive terminal. The bundled Qt preset compiles Qt Base from source into `openimage-desktop`. See the [build instructions](docs/BUILD.md), [format specification](docs/FORMAT.md), and [qualification evidence](docs/QUALIFICATION.md).

## Windows development artifacts

The [Windows x64 package](artifacts/OpenImage-0.1.0-windows-x64-dev.zip) includes the CLI and Qt desktop executables. See the [build instructions](docs/BUILD.md) for runtime requirements and the [release blockers](docs/RELEASE-BLOCKERS.md) for scope. Screenshots show the [Professional](artifacts/screenshots/professional.png) and [LCARS](artifacts/screenshots/lcars.png) appearances.

