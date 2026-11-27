# Shared open-file descriptions (#1002)

This audit starts from pTOS `20db343d71d1b9bae51947f921ca70154407ddce`. The
existing PROCESS descriptor namespace is the starting point for the work.
A binding grants one close right and identifies an SFT slot and incarnation.
Bindings that share that SFT owner also share the backend's open position.
The owner and dispatch below the binding are the migration boundary.

## Integration sequence

1. Record current descriptor admission, cursor placement, dispatch and final
   release for FAT, PFS and BIOS devices. Identify the FreeMiNT adapter boundary.
2. Introduce a typed backend interface on the existing SFT owner and preserve
   its exact incarnation and reference accounting throughout the transition.
3. Migrate file I/O and standard-stream dispatch in increments, covering FAT
   and virtio-9p and comparing managed and legacy caller behavior.
4. Coordinate the `FILEPTR`/`DEVDRV` adapter with #978 and define the retention
   boundary required by #929. Validate the result on native MMU and small-ROM
   configurations before removing transitional paths.

Keep `OPNFILES`, the GEMDOS handle ABI and the FAT `OFD` size constraint.
The descriptions and their backend references will continue to use the
existing PROCESS admission and retirement mechanisms. Directory iteration
and DTA/search ownership remain under their current contracts.
