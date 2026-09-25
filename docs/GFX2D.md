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

The software cursor in `kernel/wm/ui.c` is skipped while
`vgpu_cursor_active()` is set. Mouse motion then sends `MOVE_CURSOR` on
`cursorq` and does not dirty the framebuffer. If the cursor command
fails, the driver prints `vgpu cursor software fallback` and the software
cursor stays.

`gfx.backend=framebuffer` never programs a scanout. The old ChrisC
`hw_gpu_arm` path is a no-op success when the kernel driver is already
live, so a userspace arm does not reset the queue.

Resource ids, DMA slots, and virtqueue descriptors used by a 2D resource
are released on detach/unref. The 1000-cycle check is `gfx.stress` /
`make test-qemu-virgl`. The short boot path checks 4 cycles.

Offscreen surfaces, scissor, and a window-surface compositor are not a
separate object model yet. Windows still paint into the screen buffer.
