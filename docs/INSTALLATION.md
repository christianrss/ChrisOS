# Installation

What the installer does at
`894aed92e48e764e2627ecfc2514684f50809f59`, and what it does not do.
`docs/REAL_HARDWARE_PLAN.md` is the target. This file is the current
behavior.

## Proven path

`test-qemu-install` passes. `docs/FOUNDATION_AUDIT.md` records the image
check `install image ok` and the installed disk printing `cfs mounted`
and `desktop 60Hz`. The stall on `install backup gpt` was fixed before
this campaign: ATA DMA completion was already set, and the CFS allocator
walked the bitmap from the start of the disk. Those fixes stay.

The kernel that boots is the one the host linked. This is not an install
of a KCC-built kernel.

## What `install_device` writes

- a GPT, including the backup written by `write_gpt_backup` (the line
  `install backup gpt` is progress, not the end of the install)
- an ESP of 16384 sectors, or 65536 sectors when the disk has more than
  200000 sectors
- FAT structures for that ESP, then `BOOTX64.EFI`, `KERNEL.ELF`, and
  `LIMINE.CFG` taken from the running CFS (`BOOT/KERNEL.ELF`,
  `EFI/BOOT/BOOTX64.EFI`, `BOOT/LIMINE.CFG`)
- a ChrisFS whose superblock sector count is `STOR_DISK_SECTORS`
  (1048576), starting at LBA `2048 + esp`

A disk smaller than `2048 + esp + STOR_DISK_SECTORS + 64` sectors is
rejected with `install too small`.

`copy_tree` copies `SYS`, `APPS`, `LIB`, `GAMES`, `BOOT`, `SRC`, and
`BIN`. It does not copy `EFI/`. The foundation note still applies: the
installed CFS selftest can skip `EFI/BOOT/BOOTX64.EFI` because that file
lives on the ESP. `BOOT/INSTALL.AUTO` is removed on the target so the
next boot does not install again.

## Selection

`install_disk(index)` refuses an index `bd_installable` rejects. That
already excludes the boot disk, the root disk, RAM disks, and partitions.

`install_auto` runs only when `BOOT/INSTALL.AUTO` exists. It reads
`BOOT/INSTALL.TARGET` and installs the installable disk whose name
matches that file. A missing name prints the disk list and does not
write. Two installable disks with the same name are refused. `BOOT/INSTALL.DRY`
prints the selection and returns without writing.

The QEMU install gate writes `BOOT/INSTALL.TARGET` containing `ahci`.
Disks still have no model or serial string in `BlockDevice`. The name
and the sector count are what the log shows.

## Not installed

- a previous-kernel file
- a Limine menu entry for that file
- a CFS sized to the rest of a 64 GiB or larger disk
- an NVRAM `Boot####` entry (the ESP fallback path is the one in use)
- confirmation that the machine was powered on with the ISO removed under
  OVMF as its own gate

Host makefiles in this tree do not write `/dev/sdX` or `/dev/nvmeXnY`.
Keep it that way.
