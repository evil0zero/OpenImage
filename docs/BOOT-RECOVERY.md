# Boot recovery design notes

The release requires a post-restore boot stage. Its input must contain a verified disk and partition identity map, source boot dependencies, target firmware mode and authorization for any writes. Record disk restoration, verified boot structures, actual boot, and external intervention as separate outcomes. The current regular-file engine does not run this stage.

## Windows

Use the BCD object model to identify boot manager, OS loader, recovery and device elements. Preserve a valid store and change only references that have a validated mapping. On Windows, evaluate the offline BCD APIs and the use of BCDBoot with version-matched resources from the restored installation. BCDBoot can rebuild stores from BCD-Template and update firmware state, so restoration must account for UEFI boot order and the selected system partition. On Linux, evaluate hivex for offline hive parsing and staged edits. BCD-SYS provides a behavioral reference for working with Windows boot files from Linux. Hive parsing alone does not resolve device elements or verify that an installation will boot. Treat hive data and BCD resources as untrusted inputs. Preserve WinRE, Secure Boot and TPM-dependent recovery behavior. [Microsoft BCD object model](https://learn.microsoft.com/en-us/previous-versions/windows/desktop/bcd/about-bcd), [Microsoft BCDBoot reference](https://learn.microsoft.com/en-us/windows-hardware/manufacture/desktop/bcdboot-command-line-options-techref-di?view=windows-11), [hivex API](https://libguestfs.org/hivex.3.html), [BCD-SYS upstream](https://github.com/jpz4085/BCD-SYS).

## GPT and Linux

Before layout changes, validate GPT primary and backup headers and partition arrays, including CRCs and geometry. Compare the restored partition identifiers against the restore plan. Inspect GRUB and systemd-boot entries, kernel and initramfs paths, and mount references. Update only known references through the validated mapping. Preserve unrelated EFI entries, firmware order and alternate OS files. Unknown encrypted-root dependencies require a recoverable stop. [UEFI GPT specification](https://uefi.org/specs/UEFI/2.10/05_GUID_Partition_Table_Format.html), [Boot Loader Specification](https://uapi-group.org/specifications/specs/boot_loader_specification/).

Qualification requires cold-booting restored disposable guests for Windows and Linux across supported firmware and layout combinations. Record host and guest versions, image and restored hashes, firmware mode, partition validation, boot console evidence and recovery state.
