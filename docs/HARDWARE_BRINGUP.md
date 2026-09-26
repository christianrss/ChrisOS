# Hardware bring-up

Protocol for the first physical machine. It has not been executed. Do not
mark REAL-HW-1 from a QEMU log.

## Before leaving QEMU

`docs/REAL_HARDWARE_PLAN.md` requires `test-qemu-installed-uefi`: install
onto an empty disk, power off, drop the ISO, boot that disk with OVMF,
and match the install and second-boot markers. `test-qemu-install` is a
different, already passing gate. It is not a substitute.

The native toolchain level-0 gate is a separate prerequisite for any
claim that the machine compiled its own kernel. Bring-up of profile 1
can use the host-built kernel. REAL-HW-3 cannot.

## Machine

- UEFI x86-64
- a disk that can be erased
- installer image on removable media
- serial attached if the board still has a header

`safe` on the kernel command line, once it exists, is the first mode:
one CPU, no JIT, no experimental drivers, no network, no audio,
framebuffer, storage, filesystem, keyboard, and a shell or editor.

## Order on the first boot of the installer

1. Confirm the firmware booted the removable media.
2. Capture COM1 if it is connected.
3. After ChrisFS mounts, keep `SYS/LOGS/BOOT.LOG` and
   `SYS/LOGS/HWINFO.LOG`. Those files are part of the protocol. The
   logger that writes them is not in the tree yet. Until it is, serial
   is the only log.
4. Run `hwinfo` and keep the output. The command is not implemented yet.
5. Read from the target disk. Write only a partition created for the
   test.
6. Check the keyboard, the mouse, and the framebuffer size against the
   mode the panel actually shows. Record pitch, width, height, bpp, and
   pixel format.
7. Select the disposable disk by model, serial, and capacity. Refuse the
   install if those strings were not shown.
8. Install. Power off. Remove the installer. Boot the disk.
9. Confirm the root device, the CFS mount, and a shell or the desktop.

If an AP, the IOAPIC, NVMe, AHCI, or USB fails, the BSP boot should
continue and the log should name the failure. That fallback is a
requirement, not a behavior this file claims to have observed.

## Panic

Record exception, CPU, RIP, RSP, CR2, error code, process, VM slot, and
build id. A backtrace is recorded when frame pointers are present. The
current panic path is whatever the kernel already prints on serial. The
on-screen report and the next-boot record are not done.

## Stop conditions

Stop if the only disk in the machine holds data that has not been copied
elsewhere. Stop if the installer offers a disk and does not show a serial
number or an equivalent stable id. Stop if a write test would touch the
installer media.
