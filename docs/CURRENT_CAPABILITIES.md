# Current capabilities

Base snapshot: `origin/feat/os2` at `a3a3340f5b4dfb1e1f899d40ffc930ee57838abd`.

A row is not `PROVEN-QEMU` or `PROVEN-HARDWARE` unless that gate was run
on this tree. Older QEMU notes in `docs/STABILITY_REPORT.md` describe an
earlier boot. They were not repeated after the TLB change.

| Capability | Class | Host tested | QEMU tested | Hardware tested | Known limitations | Gate |
| --- | --- | --- | --- | --- | --- | --- |
| Boot via Limine, host GCC kernel | IMPLEMENTED | kernel sources compile under the host makefile when the toolchain is present | not re-run here | no | SH0 image. Not a self-hosted kernel | `make kernel` |
| PMM and heap locks | HOST-TESTED | `host-pmm-heap-smp-test` existed before this change; not re-run in this pass | not re-run | no | No hardware leak loop | `host-pmm-heap-smp-test` |
| TLB shootdown membership | HOST-TESTED | `host-tlb-proto-test` | not re-run | no | Reuse needs invalidate and halt. The kernel sends an NMI; that path was not booted. With the local APIC off, a job that never returns keeps its frames quarantined | `host-tlb-proto-test` |
| Kernel ring log | HOST-TESTED | `host-klog-test` | not booted | no | 8192 bytes. `dmesg` shows the tail. `SYS/BOOT.LOG` is written only when ChrisFS is the backend. That write was not booted | `host-klog-test` |
| Build identity | HOST-TESTED | `host-buildinfo-test`, `host-buildstamp-test` | not booted | no | Date is baked in at host compile time, so two builds of the same git commit differ. The SHA-256 is of the image with the hash slot still zero | `host-buildinfo-test` |
| Task slot reuse | HOST-TESTED | 1000 open/close | not re-run | no | At most 32 windows exist at once (`TASK_MAX`). The gate reuses slots. It does not touch the PMM | `host-task-window-test` |
| PMM alloc/free cycle | HOST-TESTED | 1000 pages, free count returns | not re-run | no | Host stub memory, not the QEMU machine | `host-pmm-cycle-test` |
| Process address space | IMPLEMENTED | ELF malformed cases have a host test from the earlier campaign | not re-run | no | User processes run on the BSP only. `proc_release_user` frees user frames after unmap on that CR3. There is no 1000-process counter gate | `host-elf-malformed-test` |
| CLVM slot close | IMPLEMENTED | `host-task-window-test` opens and closes 24 windows | not re-run | no | No heap-versus-PMM comparison. Not 1000 cycles | `host-task-window-test` |
| JIT free | IMPLEMENTED | protocol test covers the reuse rule JIT now calls | not re-run | no | Quarantine list is 128 entries. Overflow keeps the frames and logs `tlb quarantine full` | `host-tlb-proto-test` |
| ChrisFS | HOST-TESTED in earlier campaign | CFS, fsck, journal, fuzz targets exist | earlier QEMU notes only | no | Not re-run after this change | `host-cfs-test` and the other CFS host targets |
| AHCI, NVMe, VirtIO block, ATA | IMPLEMENTED | no new host driver test | earlier QEMU notes only | no | Block size other than the driver's current path is not a new gate here | `test-qemu-ahci`, `test-qemu-nvme`, `test-qemu-vblk`, `test-qemu-ata` |
| xHCI | IMPLEMENTED | no | earlier `test-qemu-xhci` is listed; not re-run | no | HID lifecycle was not re-audited line by line in this pass | `test-qemu-xhci` |
| Framebuffer desktop | IMPLEMENTED | no | earlier desktop marker; not re-run | no | Pitch is not proven across 800x600 through 1920x1080 | QEMU desktop gates |
| VirtIO-GPU 2D | EXPERIMENTAL | no | `test-qemu-gpu` exists; not re-run | no | Not a finished resource lifecycle. VirGL is unsupported | `test-qemu-gpu` |
| KCC | EXPERIMENTAL | `host-kcc-test` compiles the level-0 fixture, `serial.c`, `klog.c`, `string.c`, `pit.c`, `meminfo.c`, a volatile MMIO fixture, and a packed-struct offset check | no | no | Six metal files compile. Inline assembly, `limine.h`, and `sizeof` still stop the rest. SH4 is not proven | `host-kcc-test` |
| ChrisAsm / ChrisLd | EXPERIMENTAL | host tests for the small assembler and linker | no | no | Do not assemble or link the real kernel | `host-chrisasm-test`, `host-chrisld-test` |
| SH1–SH6 | UNSUPPORTED as a proven level | no in-OS gate | no | no | See `docs/CURRENT_SELFHOST_AUDIT.md` | none |
| Physical machine | UNPROVEN | no | no | no | No `PROVEN-HARDWARE` | none |
