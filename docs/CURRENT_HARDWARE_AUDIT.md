# Current hardware audit

Profile target: x86-64, UEFI, Limine, ACPI, PCI, LAPIC and IOAPIC, SMP with
a BSP-only fallback, NVMe or AHCI, xHCI HID with PS/2 fallback, GOP
framebuffer, GPT plus ChrisFS.

Nothing below is `PROVEN-HARDWARE`. No physical machine was booted.

| Piece | Class | Notes |
| --- | --- | --- |
| UEFI plus Limine | IMPLEMENTED | The host image is built for that path. Not re-booted in this pass. |
| ACPI RSDP, MADT, MCFG | EXPERIMENTAL | `kernel/metal/acpi.c` exists. Discovery is not shown to be free of QEMU constants by a new gate. |
| LAPIC, IOAPIC | EXPERIMENTAL | IPI vector `0xF0` is the TLB poke. IOAPIC routing was not re-tested. |
| SMP | EXPERIMENTAL | Earlier logs reported `cpu_online_count=4`. Not re-run. `nosmp` exists. |
| AHCI, NVMe, ATA, VirtIO block | EXPERIMENTAL | Drivers and older QEMU markers exist. Timeouts return errors on several paths (`docs/STABILITY_AUDIT.md`). 4096-byte blocks are not a new gate. |
| xHCI | EXPERIMENTAL | `kernel/fs/xhci.c` is in the tree. `docs/HARDWARE_COMPATIBILITY.md` still says unsupported and is older than the source. This pass did not re-run `test-qemu-xhci`. |
| USB HID | EXPERIMENTAL | Not separated as a core shared with UHCI. Not re-tested. |
| PS/2 | EXPERIMENTAL | `kernel/metal/ps2.c`. Desktop input on QEMU. Not proven without PS/2. |
| GOP framebuffer | EXPERIMENTAL | Limine framebuffer. Generic code must not assume `pitch == width * 4`; that rule is not enforced by a multi-mode gate. |
| Safe mode | EXPERIMENTAL | `safe`, `nosmp`, `noapic` and related flags exist. `test-qemu-safe` was not re-run. |
| Installer | EXPERIMENTAL | Explicit disk selection was fixed earlier (`aa5186a`). `test-qemu-install` was not re-run. |
| VirtIO-GPU | EXPERIMENTAL | See `docs/CURRENT_GRAPHICS_AUDIT.md`. |
| Wi-Fi, Bluetooth, vendor GPU acceleration, VirGL | UNSUPPORTED | Out of profile. |
