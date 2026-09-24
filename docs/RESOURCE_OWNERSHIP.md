# Resource ownership

Who frees a resource when an app or process dies.

| Resource | Owner | Destroyed by |
| --- | --- | --- |
| Process PML4 and user-half page tables | the process (`Proc.cr3`) | `proc_destroy` → `mm_free_user_space` |
| User leaf frames (stack, heap, framebuffer, ELF) | `Proc.pages` | `proc_release_user` unmaps with `mm_unmap_cr3`, then `pmm_free` |
| CLVM guest memory | the slot / VM | existing slot teardown (not changed here) |
| JIT code pages | the JIT VA slot | `jit_free`: unmap, TLB shootdown, VA freelist, then `pmm_free_contig` |
| Task framebuffer | the task | `task_close` clears the slot. `host-task-window-test` opens and closes 24 windows. Heap versus PMM bytes are not compared |
| Z-buffer | the gfx slot (`ctx->zbuf`) | `clvm_sys_dispatch` binds that buffer for the call. The global pointer is only the current binding |
| Native file descriptor | `g_ufile[fd].owner` process | `syscall_close_owner` from `proc_destroy` |
| CLVM file descriptor | `g_fds[fd].slot` | `clvm_sys_close_slot` → `fd_free` (flush failure is returned) |
| Socket | `Sock.owner` process and `Sock.slot` app | `sock_close_proc` and `sock_close_slot` |
| Kthread stack | the `KT` slot | `kthread_join` frees the stack after the thread is done |
| CLVM mutex/cond wait | `(slot, guest address)` | slot teardown must drop waiters; identity no longer collides across VMs |
| AC97 DMA pages | the device | `ac97_init` frees the first page if the second allocation fails. No unload path. |
| ATA DMA bounce and PRDT | the ATA driver (`g_ata_dma_phys`) | boot lifetime. The IDE port is single-owner, so the bounce is not shared across callers |
| CFS readahead buffer | kernel, only while `g_cfs_lock` is held | not freed; not used from an interrupt |
| Device queues | the driver | not changed |
| Camera, light, texture binding | `ClvmGfxCtx.view3d` | loaded at syscall entry, saved on return. A context that has never run starts from the default camera `(0, 1.5, 5)`, light, and texture slot 1. Screen size stays the viewport |
| Voxel world | one global world, `g_voxel_owner` | `voxel_claim` rejects a second slot. `clvm_sys_close_slot` releases it. There is not a world per app |
| Mouse capture | `g_capture_task` | ESC, focus loss, `clvm_sys_close_slot` |

A process that fails `proc_create` after the address space exists calls
`proc_destroy`, which frees the stack page, the page tables, and that
process's descriptors and sockets.
