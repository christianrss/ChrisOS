# Hardware compatibility

Classification of this tree at
`894aed92e48e764e2627ecfc2514684f50809f59`. "Supported" means a gate in
this repository passed. "Experimental" means code exists and a gate has
not shown it on the hardware it names. "Detected" would mean a probe
printed the device and did not claim a driver. "Unsupported" means there
is no driver. Real machines are not in the tested set.

| Area | Class | Evidence |
| --- | --- | --- |
| Host GCC kernel, Limine ISO boot in QEMU | supported | existing kernel and QEMU gates, including the foundation install boot |
| AHCI install target in QEMU | supported | `test-qemu-install` in `docs/FOUNDATION_AUDIT.md` |
| ATA in QEMU, 1 CPU and 4 CPUs | supported | `test-qemu-ata` reached `desktop 60Hz` |
| VirtIO block and virtio-gpu | experimental, QEMU only | drivers exist; they are not profile 1 |
| NVMe | experimental | `kernel/fs/nvme.c` exists; no real-device gate and no namespace LBA-size gate |
| Legacy ATA on a physical machine | experimental | QEMU only |
| PS/2 keyboard and mouse | experimental | used by the QEMU desktop; not proven on a machine without PS/2 |
| UEFI GOP framebuffer | experimental | Limine provides a framebuffer in QEMU; pitch and pixel format are not covered by a multi-mode gate |
| ACPI, APIC, IOAPIC | experimental | `kernel/metal/acpi.c`, `apic.c`, `ioapic.c`; MADT is not the sole tested topology |
| UHCI mass storage | experimental | `kernel/fs/usb_msc.c` |
| xHCI | unsupported | no source file |
| USB HID keyboard and mouse | unsupported | no source file |
| Intel, AMD, and NVIDIA GPU acceleration | unsupported | no driver; the framebuffer path is the one profile 1 uses |
| Wi-Fi, Bluetooth, audio as a hardware-profile requirement | unsupported | not required, and not claimed |

SMP on QEMU with 4 CPUs has booted to the desktop. That is not a MADT
failure-isolation test. A failed AP must not be described as supported
recovery until a gate shows the BSP continuing with a diagnostic.
