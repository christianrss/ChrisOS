# Foundation audit

Campaign start HEAD on `feat/os2`: `dc662538477d7322312a2d54c983207b5b6259d7`.
No commits existed after that SHA on `origin/feat/os2`. This file was written
by reading the tree at that SHA and comparing it with `docs/STABILITY_AUDIT.md`,
`docs/STABILITY_REPORT.md`, `docs/LOCKING.md`, and `docs/RESOURCE_OWNERSHIP.md`.
Those documents describe the previous campaign. They are not treated as proof.

Work branch: `cursor/foundation-campaign-7c6f`.

Severity: P0 corrupts memory or the filesystem, breaks isolation, or panics on
a hostile or ordinary input. P1 breaks a core path or leaves a race that can
panic. P2 is recovery, leaks, or an edge that is contained. P3 is structure
without a known integrity failure.

| ID | severity | subsystem | symptom | root cause | owner | concurrency model | test | status | residual risk |
|---|---|---|---|---|---|---|---|---|---|
| INST-01 | P0 | installer | `test-qemu-install` ends on `install backup gpt` and never prints `install auto` | `install_selftest` ran first and copied `BOOT/KERNEL.ELF` through ATA. `write_gpt_backup` prints that line before `write_esp`. On SMP the DMA completion bit was already set before the first poll, so every transfer burned a full timeout. `block_alloc` also rescanned the bitmap from zero | installer owns the RAM image; ATA driver owns the DMA bounce; `Cfs.alloc_hint` is mount-local | BSP does the copy. APs enable the LAPIC only after `sti` | `test-qemu-install` | PASS. Image check `install image ok`. Installed disk printed `cfs mounted` and `desktop 60Hz` | `copy_tree` does not include `EFI/`, so the installed CFS selftest skips `EFI/BOOT/BOOTX64.EFI`. The ESP still boots. The marker is unlinked on the target |
| ATA-DMA-01 | P1 | ATA | install read does not finish | a finished DMA whose status bit was already set never counted as idle, and each transfer allocated a new PRDT | device-local bounce, same single-owner rule as the IDE registers | one transfer at a time; the port is not re-entrant | SMP4 ATA boot and the install gate | PASS on those boots. No `ata dma timeout` flood | bounce is never freed; a second concurrent caller would alias it |
| CFS-RA-01 | P2 | CFS | sequential reads issue one sector command each | `cfs_read_at` called `cache_read` per sector even when LBAs were contiguous | buffer `g_cfs_ra` is kernel-global and used only under `g_cfs_lock` | CFS yielding lock | `host-cfs-test` | host test passed | non-contiguous files still read one sector at a time |
| CHRISC-PTR-01 | P0 | ChrisC / LIB | overlap `memmove` goes the wrong way once an address has bit 31 set | `(int)` is a 32-bit signed narrow (`gen_narrow` SHL/SAR by 32). `LIB/STRING.CC` compared those narrowed values | guest pointers are VM-local addresses | per VM | `test_chrisc_ptrwidth` | host test passed | other `(int)` casts of coordinates in `SIM.CC` / `HIT.CC` are voxel indices, not addresses |
| TLB-IPI-01 | P1 | MM | a CPU that stops calling `mm_tlb_poll` trips `tlb shootdown timeout`, and a frame can be reused while a remote TLB entry remains | shootdown only bumped `mm_tlb_gen` and spun. `irq_dispatch` returned immediately for vector >= 48, and the LAPIC was left software-disabled | MM lock holder waits; each CPU acks its own `mm_tlb_seen` slot | IPI vector 0xF0 plus the old poll. APs take interrupts only after install | SMP4 ATA boot reached `desktop 60Hz` with no shootdown panic | boot passed. No test yet forces a remote CPU to ack by IPI while it is not polling | poll still covers the install window, when APs have IF clear |
| PROC-BSP-01 | P2 | proc | user processes are not scheduled on APs | `proc_switch` panics if `smp_current_cpu() != 0`; `syscall_dispatch` returns an error off the BSP | process table is kernel-global, BSP-only by invariant | BSP | no dedicated lifecycle loop yet | OPEN invariant, already enforced | create/destroy PMM cycle test is not in tree |
| USERCOPY-01 | P1 | syscall | a bad user pointer must not panic the kernel | not every syscall goes through one `copy_from_user` helper | process address space | BSP | no QEMU hostile-pointer case yet | OPEN | `SYS_WRITE` max length is already rejected |
| TYPE-01 | P2 | ChrisC | `is_float` on a pointer symbol means the pointee is float | flags instead of an explicit `TypeDesc` | compiler-local | single threaded compile | `test_chrisc_ptr_float` | OPEN, behavior fixed for the known cases | internal representation not migrated |
| CLVM-ISO-01 | P1 | CLVM | two VMs with the same guest address must not share host resources | sync identity is `(slot, guest address)` in the previous campaign; teardown of FDs, gfx, and mouse is not covered by one repeated test | per VM / per slot | per slot | `host-clvm-sync-test` only | OPEN | |
| JIT-DIFF-01 | P1 | JIT | interpreter and JIT can diverge without a suite that compares them | no differential runner | per-CPU trampoline; compile scratch is serial under the JIT lock | per CPU, compile lock | none | OPEN | |
| NET-BSP-01 | P2 | net | undefined if an AP touches the NIC | not an explicit tested invariant | device, BSP for this campaign | BSP-only | none | OPEN decision: stay BSP-only | |
| CI-01 | P2 | CI | gates exist only as make targets | no `.github/workflows` | repo | n/a | n/a | OPEN | |
| FS-LOCK-01 | P3 | CFS | global filesystem lock | correctness over parallel writers | `g_cfs_lock`, owner is `smp_current_cpu()+1`, yielding, re-entrant | one writer | `host-cfs-lock-test` | accepted for this phase | |

## State classes checked against the tree

- Global read-only after boot: Limine response, kernel CR3 value published as `kernel_cr3`.
- CPU-local: `smp_current_cpu`, JIT sys trampoline `g_jit_ctx[cpu]`, CFS lock owner id, `mm_tlb_seen[cpu]`.
- Process-local: user CR3 and the process page list. Scheduling is BSP-only (`PROC-BSP-01`).
- VM-local: CLVM memory, heap offset, fault code. Sync objects are keyed by slot and guest address, not by the guest address alone.
- Device-local: ATA DMA bounce (`g_ata_dma_phys`), AHCI port windows, VirtIO queues.
- Filesystem global: one mounted CFS, `g_cfs_lock`.
- Kernel global locked: PMM (same-CPU recursive), heap (not recursive), MM lock (spins and polls TLB).

## Installer path, as read

`kmain` clears interrupts, mounts the root, calls `install_selftest`, then `install_auto`. `write_gpt_backup` prints `install backup gpt` and returns. `write_esp` then reads `EFI/BOOT/BOOTX64.EFI`, `BOOT/KERNEL.ELF`, and `BOOT/LIMINE.CFG` with `fs_read_at` of 2048 bytes. On the install gate the root disk is ATA. On `test-qemu-noata` the root is AHCI and the same selftest reaches `install selftest ok`. The selftest is skipped when `BOOT/INSTALL.AUTO` exists, which is the explicit install request. The no-ATA gate does not plant that file.

## Gates run on this branch

- `test_chrisc_ptrwidth` passed.
- `host-cfs-test`, `host-cfs-indirect-test`, and `host-cfs-maxwrite-test` passed after `alloc_hint`.
- `test-qemu-ata` with 1 CPU and with 4 CPUs reached `desktop 60Hz`.
- `test-qemu-install` passed: `install tree copied`, `install gpt+esp+cfs disk=ahci`, `install image ok`, then the installed disk printed `cfs mounted` and `desktop 60Hz`.

## Not claimed

`foundation-gates`, Mine, Doom, self-host level 4, a hostile usercopy QEMU case, and a test that distinguishes IPI ack from poll ack are not results yet.
