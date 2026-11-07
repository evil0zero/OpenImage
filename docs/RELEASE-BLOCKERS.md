# Release blocker report

**Status: blocked for production release.**

| Area | Evidence and impact | Required correction and verification |
| --- | --- | --- |
| Physical storage and privilege separation | Windows discovers disk interfaces and partitions and supports exact raw I/O on eligible offline disks. The raw round trip passed on disposable VHDs. Disk I/O runs in the elevated application process. | Qualify real hardware, implement Linux device access and a separately authorized worker, and test device replacement, disconnection and active-write interruption. |
| Filesystems and migration | The engine preserves the entire input byte stream. Filesystem detection, allocated extents, extraction, resizing, snapshots, partition validation and boot dependencies are absent. | Implement and individually qualify each supported operation and fallback, including unsupported encrypted and multi-device layouts. |
| Boot repair | The engine contains no BCD, ESP, GRUB or systemd-boot recovery stage. | Implement and qualify on restored Windows and Linux guests, including UEFI, BIOS and dual boot. Record actual guest boots. |
| Container and security | Version 1 covers a sequential raw stream with unkeyed digests. Encryption, Argon2id, isolation, incremental chains and durable resume are absent. | Add authenticated encryption and key handling with a vetted dependency. Fuzz the parser and isolate decoders. Validate backup/restore journals and revalidate identity and prior data before resume. |
| Staging trust | Output uses a private staging directory and no-replace publication. Ancestors are resolved by path and the Linux input lock is advisory. | Bind directory and file handles across operations and qualify adversarial path changes, filesystem durability and concurrent source modification. |
| UI and recovery media | Both desktop dashboards display native Windows inventory and partition maps and support eligible raw-disk workflows. Full wizard screens, advanced codec controls, unified GUI selection and recovery-media generation are absent. | Qualify the source-built Qt profile on Linux, validate both designs through the complete workflows, and produce a bootable offline restore environment. |
| Compression and throughput | Built-in codec choices use fixed parameters and one serial stream. | Implement the bounded parallel pipeline, configurable resource budgets, codec isolation and profiles. Execute measured hardware and GUI latency gates. |
| Release qualification | See the test report for executed tests. Full mandatory fault injection, platform, filesystem, security, performance and boot qualification remains open. | Obtain the Linux and VM test environments and licensed Windows media. Close the remaining tests, dependency provenance and reproducibility gates before release. |
| Binary distribution | The repository records Qt and codec notices. OpenImage's own distribution license and final static Qt license review are outstanding. | Record the project license and verify applicable corresponding source and relinking obligations before publicly distributing the new binary. |

## Recovery for the regular-file build

For an invalid image, keep the original source or a previously verified backup, verify that copy, and restore to a fresh filename. If an operation reports an existing destination, verify its contents independently or use a fresh path. For an interruption, inspect the destination and verify any published image or compare any published restored file before relying on it. A private .openimage-* directory left in the output directory after termination is staging data. After confirming the originating process has exited, remove that directory and restart from the unchanged source. The build has no resume support.

