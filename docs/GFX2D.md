# 2D graphics

The framebuffer API is `kernel/gfx/graphics.c` and `kernel/gfx/gfx2d.c`.
Pixels are 32-bit, pitch comes from Limine, and the window manager draws
into the front buffer.

When VirtIO-GPU is live, `hw_gpu_flush_rect` copies the dirty rectangle
from the front buffer into the scanout backing and issues
`TRANSFER_TO_HOST_2D` plus `RESOURCE_FLUSH` for that rectangle. A full
frame is counted separately from a partial rectangle
(`g_rects`, `g_pixels`, `g_bytes`, `g_full`, `g_partial` in the driver).
Those counters are not printed every frame.

The cursor resource is 64×64. QEMU allocates a 64×64 sprite and ignores
any other size, which used to leave a transparent pointer in place of the
desktop cursor. `SET_SCANOUT` drops that sprite, so the next command after
a scanout change is `UPDATE_CURSOR` rather than `MOVE_CURSOR`.

While that submit succeeds, `ui_draw_cursor` does not paint the software
cursor. If the cursor queue is busy or the kick fails, the same frame
draws the software cursor. A failed kick clears the hardware cursor and
prints `vgpu cursor software fallback` (setup) or `vgpu cursor fallback
software` (later motion). The PS/2 mouse is also drained each frame, and
the GPU IRQ handler chains the previous handler so it cannot replace the
mouse line.

`gfx.backend=framebuffer` never programs a scanout. The old ChrisC
`hw_gpu_arm` path is a no-op success when the kernel driver is already
live, so a userspace arm does not reset the queue.

Resource ids, DMA slots, and virtqueue descriptors used by a 2D resource
are released on detach/unref. The 1000-cycle check is `gfx.stress` /
`make test-qemu-virgl`. The short boot path checks 4 cycles.

Offscreen surfaces, scissor, and a window-surface compositor are not a
separate object model yet. Windows still paint into the screen buffer.
