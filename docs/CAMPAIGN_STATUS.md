# Campaign status

Initial HEAD: `33390e4285f6e5f00a386482584efec14032d80e` on `origin/feat/os2`.

This pass does not call the tree stable, self-hosted, or hardware-ready.

## Blockers by phase

### Phase 1 — kernel foundation

| ID | Sev | State |
| --- | --- | --- |
| TLB-SKIP-01 | P0 | Fixed in this pass. A silent CPU is fenced. Frames are not reused until it halts. Host gate: `host-tlb-proto-test`. |
| TLB-HALT-01 | P1 | Open. Halt is at the worker loop. No NMI. |
| TLB-QUAR-01 | P1 | Open. Quarantine is capped at 128. |
| LIFE-01 | P1 | Open. No 1000-cycle process, CLVM, JIT, or window gate with counters. |
| ACCT-01 | P2 | Open. Page counters exist. No `meminfo`. |

### Phase 2 — gates and observability

Host gates already exist (`make host-gates`, `make qemu-gates`, `make full-gates`).
There is no GitHub Actions workflow. There is no build id, `dmesg` ring, or
rich panic record. P2 relative to the TLB bug. Not done.

### Phase 3 through 5 — toolchain, SH4, SH5

P0 blocker: KCC cannot compile a real kernel file. ChrisAsm cannot assemble
the privileged instructions the kernel uses. ChrisLd has not produced the
boot ELF. SH4 and SH5 are not proven. Not started beyond the audit.

### Phase 6 — hardware profile

Drivers exist for several QEMU devices. No new QEMU run and no hardware
run. Profile 1 is not closed.

### Phase 7 — VirtIO-GPU 2D

Experimental probe and scanout. Resource lifecycle, damage, resize, and
the stress gate are open. VirGL stays unsupported.

### Phase 8 and 9 — SH6 and real hardware

Blocked on SH4 and SH5. No physical boot. Hardware stays unproven.

## Gates this pass

| Gate | Result |
| --- | --- |
| `host-tlb-proto-test` | PASS (`tlb proto tests passed`) |
| `host-klog-test` | PASS |
| `host-buildinfo-test` | PASS |
| `host-buildstamp-test` | PASS |
| `host-meminfo-test` | PASS |
| `host-pmm-cycle-test` | PASS |
| `host-task-window-test` | PASS |
| `host-job-saturate-test` | PASS |
| `host-kthread-smp-test` | PASS |
| Freestanding compile of `tlb_proto.c`, `mm.c`, `smp.c`, `job.c`, `jit.c` | PASS |
| `make host-gates` | not run as a whole |
| `make qemu-gates` | not run |
| Hardware | not run |
