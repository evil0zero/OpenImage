# Qualification

## Windows x64 regular-file engine

The release-mode core and CLI built with MSVC 19.44.35229, Windows SDK 10.0.26100, CMake 4.4.4 and Ninja on Windows 11 build 26300. Python 3.12.10 drove the tests. All seven integration test methods passed in 2.19 seconds with hard-link creation enabled. The routine restricted execution passed six and skipped the hard-link case because Windows refused to create the fixture. The test matrix covered:

- Backup, restore, verification and independent SHA-256 comparisons for raw, Zstandard, LZ4 and LZMA2.
- Comparison against altered restored data, corrupted payload and commit records, and out-of-range lengths.
- Existing and hard-linked destinations, simultaneous publication, and source preservation.
- Directory rejection, unknown codec rejection, and Unicode filenames.

## File benchmark

Three runs per codec used 32 MiB of alternating zero and seeded pseudorandom one-MiB chunks on a 22-logical-processor Intel Windows 11 host. All restored files passed independent SHA-256 comparison. Throughput includes process overhead and the implemented readback passes.

| Codec | Median backup MiB/s | Median restore MiB/s | Image / input |
| --- | ---: | ---: | ---: |
| Raw | 84.2 | 66.7 | 1.0000 |
| Zstandard | 118.5 | 91.9 | 0.5001 |
| LZ4 | 117.3 | 75.4 | 0.5040 |
| LZMA2 | 17.5 | 73.6 | 0.5002 |

The same-directory Python copy took about 13.5 ms at the median, about 2,366 MiB/s. That copy omits data flush and readback and is not a matched storage baseline. The 80% throughput gate is **NOT TESTED**. CPU, peak memory, p95 GUI latency and NVMe/SATA/USB device measurements are also **NOT TESTED**.

## OS and boot qualification

| Environment | Result |
| --- | --- |
| Windows 11 x64 regular-file round trip | PASS |
| Linux x86-64 native build and execution | NOT TESTED |
| Windows restore and actual UEFI/GPT or BIOS/MBR boot | NOT TESTED |
| Linux restore and actual UEFI/GPT or BIOS/MBR boot | NOT TESTED |
| Cross-OS restore, encrypted root and dual boot | NOT TESTED |
| Filesystem-specific integrity, migration and extraction | NOT TESTED |
| Recovery-media boot and restore | NOT TESTED |

No physical storage device was used as an imaging target. See the release blocker report for required corrections and the failure-injection report for scenario coverage.

## Dependency replacement

An isolated copy of the source built successfully with LZ4 1.9.4 in the root-level lz4 directory, overriding bundled LZ4 1.10.0. CMake's source selection and Ninja's compiler input both show the override. The replacement passed six integration methods including the four-codec round trip, with the hard-link case skipped in the restricted execution. The packaged default build uses LZ4 1.10.0.

## Windows Qt desktop

Qt 6.8.3 Widgets and Concurrent built with MSVC x64. Both appearance choices rendered and were captured with the Windows Qt backend. The screenshots document the regular-file form. Full workflow, keyboard accessibility, HiDPI and GUI latency qualification are **NOT TESTED**.
