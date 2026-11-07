# OpenImage

OpenImage 0.2.0 is a Windows x64 preview. The desktop and terminal discover physical disks automatically. The desktop includes Professional and LCARS dashboards with partition and mounted-volume information. Backup, restore and clone support exact whole-disk transfers on eligible Windows disks. The regular-file engine supports backup, restore, clone, verification, comparison and OMI header inspection. See the [qualification report](docs/QUALIFICATION.md) and [release blockers](docs/RELEASE-BLOCKERS.md).

    openimage backup offline.raw backup.omi zstd
    openimage verify backup.omi
    openimage restore backup.omi restored.raw
    openimage compare backup.omi restored.raw
    openimage clone offline.raw cloned.raw
    openimage explore backup.omi

Keep inputs offline and stable throughout each operation. Use a trusted local output directory and choose a new filename. Use `openimage --tui` in an interactive terminal. The bundled Qt preset compiles Qt Base from source into `openimage-desktop`. See the [build instructions](docs/BUILD.md), [format specification](docs/FORMAT.md), and [qualification evidence](docs/QUALIFICATION.md).

## Windows raw disk imaging

The desktop requests administrator privileges at launch. Run the CLI as an administrator for disk I/O. Online data disks are opened with exclusive volume locks. Close programs using the disk before starting. Destination volumes are dismounted after locking. The running Windows disk requires a recovery environment. After changing disk state, refresh the desktop or rerun `openimage disks` and verify the destination against the current device list. The desktop asks you to type the destination device path before a raw write.

    openimage disks
    openimage disk-backup "\\.\PhysicalDriveN" backup.omi zstd
    openimage disk-restore backup.omi "\\.\PhysicalDriveN" --overwrite
    openimage disk-clone "\\.\PhysicalDriveN" "\\.\PhysicalDriveM" --overwrite

Raw restore and clone overwrite the whole destination and require an exact capacity match. Clone checks the logical sector sizes. The OMI v1 format does not record the source sector size, so check it before a physical restore. Interrupted raw writes can leave a partially overwritten target. Bootability and live-system snapshots are outside this preview's qualification.

## Previous Windows development artifacts

The [Windows x64 package](artifacts/OpenImage-0.1.0-windows-x64-dev.zip) includes the CLI and Qt desktop executables. See the [build instructions](docs/BUILD.md) for runtime requirements and the [release blockers](docs/RELEASE-BLOCKERS.md) for scope. Screenshots show the [Professional](artifacts/screenshots/professional.png) and [LCARS](artifacts/screenshots/lcars.png) appearances.

