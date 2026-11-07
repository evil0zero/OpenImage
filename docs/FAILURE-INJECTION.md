# Failure-injection evidence

See the recorded integration test output for pass/fail status. All executed cases use regular files. CLI exit 1 denotes an operation error, while exit 2 denotes invalid command arguments.

| Scenario | Injection and detection | Protection, persisted state and recovery |
| --- | --- | --- |
| Corrupted chunk | Change the first raw payload byte and invoke verify and restore. The decoded chunk hash must fail with exit 1. | Assert no destination or staging directory and unchanged input. Report chunk checksum mismatch. Obtain an intact backup, verify it and retry to a new path. |
| Malicious metadata | Change the signature, reserved flags, codec, logical length or stored length. Check verify and restore for exit 1. | Assert no new destination and unchanged input. Report invalid metadata or unsupported version or feature. Replace the image with a verified copy. |
| Missing or corrupt commit | Truncate the trailer, modify the commit digest or append bytes. Verify and restore must exit 1. | Assert no destination and unchanged input. Report truncation, trailing bytes or commit validation failure. Recreate the backup or use a verified copy. |
| Aliased destination | Use the source name or a hard link to the source as an output. Expect exit 1 on destination validation. | Assert both names still refer to the original bytes. Report existing destination and select a new filename. |

## Mandatory scenarios with no observed result

| Scenarios | Status |
| --- | --- |
| 1 source disk disconnection, 2 destination disk disconnection | NOT TESTED |
| 3 power loss during backup, 4 power loss during restoration | NOT TESTED |
| 6 compression worker crash, 7 LZMA2 memory exhaustion | NOT TESTED |
| 8 VSS failure, 9 Linux snapshot failure | NOT TESTED |
| 10 destination capacity exhaustion, 11 disk identity change | NOT TESTED |
| 12 missing external decoder, 13 corrupted GPT/BCD metadata | NOT TESTED |
| 14 denied UEFI changes, 15 BitLocker recovery, 16 locked LUKS | NOT TESTED |
| 17 missing incremental base, 18 network interruption | NOT TESTED |
| 19 unsupported shrinking | NOT TESTED |
| 21 termination during commit, 22 corrupt restore journal | NOT TESTED |
| 23 changed device geometry, 24 cancellation during active disk writes | NOT TESTED |

The executed regular-file cases cover corruption (5) and selected malformed metadata (20). Missing commit detection alone does not qualify process termination during commit or power-loss durability.
