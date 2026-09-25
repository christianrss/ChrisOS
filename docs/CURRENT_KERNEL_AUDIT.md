# Current kernel audit

HEAD audited: `33390e4285f6e5f00a386482584efec14032d80e` (`origin/feat/os2`).
The TLB change is the only kernel behavior change in this pass.

## SMP, MMU, TLB

`kernel/metal/smp.c` starts APs on dedicated stacks. The CPU index is passed
in a register before the stack switch. `smp_current_cpu` reads that index
from the stack pointer. That closed the earlier bug where every AP answered
a shootdown as CPU 0.

`kernel/metal/mm.c` at that HEAD still timed out and printed
`tlb shootdown skip`, then returned. Callers could reuse the physical page
while a CPU that had not acked still held a stale TLB entry. The wait also
treated CPUs as the prefix `0 .. cpu_online_count-1`. Fencing one CPU in
the middle would have waited for the wrong set.

This pass replaces that wait with `kernel/metal/tlb_proto.c`:

```text
publish generation and range
send IPI 0xF0 to each online CPU that has not acked
each CPU invlpg the range and stores the generation
the initiator returns only when every ONLINE CPU has acked
a CPU that stays silent past its quiet budget becomes FENCED
a fenced CPU is not online
its frames are not reused until that CPU marks itself halted
```

`jit_free` quarantines frames when the shootdown returns -1.
`mm_tlb_reap` frees them after `tlb_reuse_ok`.

The fence point is the AP worker loop in `job_worker_forever`. There is no
NMI halt. A CPU that is inside a job, with interrupts clear, can keep
running until that job returns. That window is still open.

## Ownership

`docs/RESOURCE_OWNERSHIP.md` is the map for process pages, JIT pages, CLVM
files, sockets, and the voxel world. Kernel address space, MMIO, and DMA
buffers are still owned by the driver that allocated them, for the life of
the boot, except the JIT path above.

`proc_destroy` drops user frames, the user half of the page tables, FDs,
and sockets. User processes are BSP-only (`proc_switch` panics on an AP).
`mm_unmap_cr3` does `invlpg` only when that CR3 is current.

## What is still open

| ID | Sev | Item |
| --- | --- | --- |
| TLB-HALT-01 | P1 | Fenced CPU halts at the next worker iteration. No NMI. A job in progress is not preempted. |
| TLB-QUAR-01 | P1 | Quarantine holds 128 ranges. Past that, frames are leaked on purpose and `tlb quarantine full` is logged. |
| LIFE-01 | P1 | `host-task-window-test` opens and closes 1000 windows and checks the live count. `host-pmm-cycle-test` allocates and frees 1000 pages. There is no combined CLVM or JIT cycle, and the window test does not use the PMM. |
| ACCT-01 | P2 | `meminfo` prints PMM, heap, and task counts. There is no CLVM or JIT counter in that line. |
| PANIC-01 | P2 | Panic prints CPU, CR3, RSP, build id, git, and the kernel hash slot. No backtrace. |
| LOG-01 | P2 | `klog` is an 8 KiB ring filled from the serial writer. `dmesg` shows the tail. After ChrisFS mounts, the tail is copied to `SYS/BOOT.LOG`. That copy was not booted. |
| BUILD-01 | P2 | Boot text includes build id, git, date, compiler, and a SHA-256 slot. The slot is filled after link by `stamp_kernel`. The hash is of the image with the slot still zero. QEMU was not booted to read it. |

P0 for this pass was the shootdown that reused frames without an ack.
That path no longer skips.
