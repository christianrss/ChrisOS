# Current kernel audit

Base: `a3a3340f5b4dfb1e1f899d40ffc930ee57838abd` (`origin/feat/os2`).
This pass adds an NMI stop for a fenced CPU. QEMU was not booted.

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

A fenced CPU is not online. Reuse also requires that CPU to have
invalidated the generation (`flushed`) and halted. Halt alone does not
release the frames.

The kernel sends that CPU an NMI (`apic_ipi_nmi`, IDT vector 2,
`nmi_entry`). The handler invalidates the published range and halts
without returning to the interrupted job. The worker loop does the same
invalidate-then-halt if it reaches the fence check first. Both paths are
in the sources. Neither was booted. If the local APIC is off, the NMI is
not sent and a job that never returns still holds its frames in
quarantine.

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
| TLB-HALT-01 | P1 | NMI stop is in the kernel and the host protocol requires invalidate plus halt. Not booted. With the local APIC off, a job that never returns is only quarantined. |
| TLB-QUAR-01 | P1 | Quarantine holds 128 ranges. Past that, frames are leaked on purpose and `tlb quarantine full` is logged. |
| LIFE-01 | P1 | `host-task-window-test` opens and closes 1000 windows and checks the live count. `host-pmm-cycle-test` allocates and frees 1000 pages. There is no combined CLVM or JIT cycle, and the window test does not use the PMM. |
| ACCT-01 | P2 | `meminfo` prints PMM, heap, and task counts. There is no CLVM or JIT counter in that line. |
| PANIC-01 | P2 | Panic prints CPU, CR3, RSP, build id, git, and the kernel hash slot. No backtrace. |
| LOG-01 | P2 | `klog` is an 8 KiB ring filled from the serial writer. `dmesg` shows the tail. After ChrisFS mounts, the tail is copied to `SYS/BOOT.LOG`. That copy was not booted. |
| BUILD-01 | P2 | Boot text includes build id, git, date, compiler, and a SHA-256 slot. The slot is filled after link by `stamp_kernel`. The hash is of the image with the slot still zero. QEMU was not booted to read it. |

P0 for this pass was the shootdown that reused frames without an ack.
That path no longer skips.
