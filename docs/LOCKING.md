# Locking

Lock order, from outer to inner. A holder may take a lock below it. It must
not take a lock above it.

1. JIT compile lock (`g_jit_compile_lock`)
2. MM lock (`mm_lock`)
3. Heap lock (`heap_lock`)
4. PMM lock (`pmm_lock`)
5. Job queue lock, kthread slot lock, AC97 lock, input state (no cross-calls)

## PMM

`pmm_lock` is a spinlock. The same CPU may re-enter it (`pmm_depth`) because
`pmm_foreach_free_run` callbacks call `pmm_claim_at` / `pmm_free`. The outer
acquisition saves interrupt state with `irq_save` so an IRQ cannot re-enter
on that CPU. PMM never takes the heap lock or the MM lock.

## Heap

`kmalloc` / `kfree` hold `heap_lock` for the whole operation, including
`heap_grow`, which allocates physical pages. That is the only heap → PMM
edge. The heap lock is not recursive.

## Page tables

`mm_lock` covers map, unmap, translate, and clone. `mm_enter` polls the TLB
generation before taking the lock so a CPU waiting for the lock can still ack
a shootdown. `map_4k` / `mm_map_cr3` may allocate page-table pages, so MM →
PMM is allowed. `mm_tlb_shootdown` holds `mm_lock` while it waits for
`mm_tlb_seen[cpu]`. Every online CPU must call `mm_tlb_poll` (job workers and
the desktop idle loop do).

User address spaces are BSP-only. `proc_switch` panics if
`smp_current_cpu() != 0`. Kernel high-half mappings are shared, so JIT unmap
uses a shootdown before the frame returns to the PMM.

## Filesystem

Not locked in this pass. Do not add a spinlock that is held while a driver
polls the disk. See FS-LOCK-01.

## Tasks and windows

Unchanged in this pass. Input capture is a single `g_capture_task` updated
from the desktop/IRQ path. It is not a second lock rank.

## Network

Socket table updates happen on the BSP syscall path and in the receive path.
There is no socket lock yet. Ownership checks are not a substitute for a lock
if an AP ever receives a packet while the BSP mutates the same slot.

## Audio IRQ

`g_ac97_lock` plus `irq_save` on the writer. The ISR takes the same spinlock
and must not sleep. Port I/O that restarts the engine happens after the lock
is dropped.

## Drivers

Storage queue locks were not changed. See DRV-TIMEOUT-01.

## JIT

Compile scratch (`g_nat`, patch sites) is global and covered by
`g_jit_compile_lock`, which is taken before any MM operation inside compile.
`jit_free` unmaps under the MM lock, shootdowns, then frees frames under the
PMM lock. The compile lock is not held across `jit_free`.
