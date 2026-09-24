# Real hardware plan

No physical machine has run this tree during this campaign. REAL-HW-1,
REAL-HW-2, and REAL-HW-3 are not started. QEMU is the prerequisite, and
the QEMU install gate that already passes still boots a host-built kernel.
See `docs/FOUNDATION_AUDIT.md` and `docs/HARDWARE_COMPATIBILITY.md`.

Limine stays:

```
UEFI firmware
  Limine EFI
  ChrisOS KERNEL.ELF
```

## Profile 1

| Piece | Requirement |
| --- | --- |
| CPU | x86-64 |
| Firmware | UEFI |
| Boot | Limine, removable path `EFI/BOOT/BOOTX64.EFI` |
| Display | GOP framebuffer from the bootloader |
| Storage | NVMe or AHCI, after the driver audit |
| Filesystem | GPT, FAT32 ESP, ChrisFS |
| Input | PS/2 or USB HID, whichever the machine has |
| ACPI | required for the hardware profile |
| PCIe | required for the hardware profile |
| Network, audio, GPU acceleration, Wi-Fi | not required |

Out of scope until a later project: NVIDIA, AMD, and Intel GPU drivers,
a full Wi-Fi stack, Bluetooth, a custom Secure Boot implementation, a
bootloader that replaces Limine, suspend and resume, full POSIX, and a
complete USB stack.

## Disk layout

Partition 1 is an EFI System Partition, FAT32, 256 MiB unless the manifest
sets another size. Partition 2 is ChrisFS for the rest of the disk, with
optional space left unallocated. The installer reads `sector_count` and
`sector_size`. It does not assume the 512 MiB `disk.img`.

```
ESP/EFI/BOOT/BOOTX64.EFI
ESP/limine.conf
ChrisFS/BIN/KERNEL.ELF
ChrisFS/SYS APPS LIB GAMES CONFIG HOME
```

The kernel path in `limine.conf` has to be the path Limine actually opens.
Today the ESP writer also places `KERNEL.ELF` on the ESP. The CFS copy and
the ESP copy must stay consistent with that config. This plan does not
claim they already match on every geometry.

ChrisFS v4 stores `STOR_DISK_SECTORS` (1048576) in the superblock and
refuses any other value. The bitmap is the same size. A 64 GiB to 2 TiB
disk needs a format revision and a migration that still mounts v4 images.
`BlockDevice.sector_count` is 32-bit. That design note is not an
implementation.

## Installer safety

The in-OS installer shows each disk's model, serial, capacity, bus, and
partitions. The operator picks one. Before any format it shows model,
capacity, and serial under an explicit warning that the disk will be
erased, and it waits for an explicit confirmation. A dry-run mode prints
the plan and writes nothing.

The boot disk is refused unless a separate, documented flow says
otherwise. "The first installable disk" is not a selection. Host recipes
do not open `/dev/sdX` or `/dev/nvmeXnY` for writes.

`install_auto` in `kernel/fs/install.c` still takes the first installable
disk. Replacing that is later work. It is recorded so it is not forgotten
behind the foundation gate.

## QEMU gate before any physical disk

`test-qemu-installed-uefi` (names may match the makefile once the recipe
exists):

1. empty disk
2. boot the installer ISO
3. install
4. power off
5. remove the ISO
6. boot the disk under OVMF

Required markers on the install boot:

```
INSTALL DISK SELECTED
GPT PRIMARY OK
GPT BACKUP OK
ESP FORMAT OK
EFI BOOT INSTALLED
CFS FORMAT OK
SYSTEM TREE COPIED
INSTALL COMPLETE
```

Required markers on the second boot:

```
UEFI DISK BOOT
CHRISOS KERNEL START
ROOT DEVICE
CFS MOUNTED
DESKTOP READY
```

The existing `test-qemu-install` PASS does not print this set and does not
remove the ISO as a separate OVMF-only boot. It is not this gate.

Run the matrix on UEFI+NVMe and UEFI+AHCI, 1 CPU and 4 CPUs, and at least
two framebuffer sizes (1024x768 and 1920x1080 when the firmware allows).
VirtIO remains a test device. It is not part of profile 1.

## Hardware work, after the toolchain level-0 gate

Discovery: CPUID, ACPI RSDP, RSDT or XSDT, MADT, MCFG when present, FADT
when a driver needs it, PCI and PCIe enumeration, BAR parsing, IRQ
routing, APIC and IOAPIC, a timer, the Limine memory map, and optional
SMBIOS for identification. A `hwinfo` command prints CPU, RAM, ACPI, PCI,
storage, USB controllers, network controllers, and the display controller.

Storage audit order: NVMe, AHCI, legacy ATA, USB mass storage. QEMU
success is not a substitute for checking DMA addresses, BARs, alignment,
cache coherency, timeouts, reset, controller enable, interrupts, polling,
doorbells, and queue sizes. NVMe must read the namespace LBA size.
ChrisFS may keep an internal logical sector, and the block device still
has to honor the real LBA size.

Input: xHCI, then enumeration, then HID keyboard and mouse, then mass
storage. `docs/XHCI.md` is written when that code exists. It does not
exist in this tree.

Graphics: use the Limine framebuffer. Record address, pitch, width,
height, bpp, and pixel format. Do not assume 1920x1080 or
`pitch == width * 4`.

SMP: local APIC ids, BSP, APs, and IOAPIC come from the MADT. If an AP
fails, boot the BSP and say so. PIC remains a fallback. MSI and MSI-X
come after polling bring-up, and polling is not the permanent design.

Timers: `timer_now`, `timer_sleep`, and `timer_deadline` in front of PIT,
and HPET, the APIC timer, or an invariant TSC when those are detected.
PIT stays as a fallback until a gate says a replacement is in use.

Boot arguments the bring-up needs: `safe`, `nosmp`, `noapic`, `noac97`,
`nonet`, `nojit`, a storage selector, `debug`, and `serial`. `safe` keeps
the BSP, the framebuffer, storage, the filesystem, the keyboard, and a
shell or editor. It turns off SMP, JIT, experimental drivers, network,
and audio.

Serial on COM1 stays when the port exists. A memory ring buffer is copied
to `SYS/LOGS/BOOT.LOG` after the filesystem mounts. `dmesg` prints the
ring. A panic screen shows the exception, CPU, RIP, RSP, CR2, error code,
process, VM slot, and build id, plus a backtrace when the frame chain is
valid. A panic record survives to the next boot when that path is
implemented. None of these commands exist as specified here.

Kernel identity on every boot, once the internal image is real:

```
ChrisOS <version>
Build: <id>
Compiler: <KCC stage or host GCC>
Kernel SHA256: <hash>
```

## Physical protocol

Use a disposable disk. Boot install media. Capture serial if the board
has it. Run `hwinfo`. Read storage. Write only a test partition. Test
keyboard, mouse, and framebuffer. Install. Power off. Remove the media.
Boot the installed disk. Logs live in `SYS/LOGS/BOOT.LOG` and
`SYS/LOGS/HWINFO.LOG`.

Installing a newer kernel preserves `KERNEL.PREV.ELF`, writes the new ELF,
syncs, and updates the Limine menu with a previous-kernel entry. The new
file is validated before the previous bootable image is no longer the one
Limine will load.
