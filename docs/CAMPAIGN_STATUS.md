# Campaign status

Base HEAD: `a3a3340f5b4dfb1e1f899d40ffc930ee57838abd` on `origin/feat/os2`.

This pass does not call the tree stable, self-hosted, or hardware-ready.

## Blockers by phase

### Phase 1 — kernel foundation

| ID | Sev | State |
| --- | --- | --- |
| TLB-SKIP-01 | P0 | Fixed in this pass. A silent CPU is fenced. Frames are not reused until it halts. Host gate: `host-tlb-proto-test`. |
| TLB-HALT-01 | P1 | NMI stop is implemented. Host protocol: halt without invlpg does not release frames. Not booted. APIC-off still depends on the worker loop. |
| TLB-QUAR-01 | P1 | Open. Quarantine is capped at 128. |
| LIFE-01 | P1 | Open. No 1000-cycle process, CLVM, JIT, or window gate with counters. |
| ACCT-01 | P2 | Open. Page counters exist. No `meminfo`. |

### Phase 2 — gates and observability

Host gates already exist (`make host-gates`, `make qemu-gates`, `make full-gates`).
`.github/workflows/host-foundation.yml` runs a subset of those host gates.
Build identity and the `klog` ring are on this tree (`host-buildinfo-test`,
`host-klog-test`). Panic still has no backtrace. `SYS/LOGS/BOOT.LOG` is not
the path in the tree (`SYS/BOOT.LOG` is). QEMU was not booted in this pass.

### Phase 3 through 5 — toolchain, SH4, SH5

P0 blocker: KCC compiles `serial.c`, `klog.c`, and a volatile MMIO fixture
on the host (`host-kcc-test`). It does not compile the kernel. A plain
`uint32_t` store of the same global is folded to the last store. A
`volatile uint32_t` keeps both 32-bit stores and both loads. ChrisAsm
still cannot assemble `cli`, `hlt`, or `invlpg`. ChrisLd has not produced
the boot ELF. SH4 and SH5 are not proven.

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
| `host-tlb-proto-test` | PASS (`tlb proto tests passed`, including halt-without-invlpg) |
| `host-kcc-test` | PASS (`test_kcc: ok`; level-0 fixture, `serial.c`, `klog.c`, link with stubs, volatile MMIO fixture) |
| `host-chrisasm-test` | PASS |
| Freestanding `mm.c`, `job.c`, `idt.c`, `apic.c`, `idt_stubs.asm` | PASS |
| `host-klog-test`, `host-buildinfo-test`, `host-buildstamp-test`, `host-meminfo-test`, `host-pmm-cycle-test`, `host-task-window-test` | not re-run on this branch |
| `make host-gates` | not run as a whole |
| `make qemu-gates` | not run |
| Hardware | not run |
