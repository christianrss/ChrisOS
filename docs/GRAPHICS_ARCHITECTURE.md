# Graphics architecture

ChrisOS keeps three presentation paths and two 3D backends.

```text
ChrisC / desktop / Mine Chris
        |
        v
kernel gfx_* and the software scene
        |
        +-- software rasterizer --> framebuffer or VirtIO 2D scanout
        |
        +-- VirGL client ---------> SUBMIT_3D --> virtio-gpu --> virglrenderer
                                                      |
                                                      v
                                                 host OpenGL
```

Applications do not see VirtIO descriptors, MMIO offsets, or VirGL opcodes.
Those stay in `kernel/gfx/`.

## Backends

| Layer | Code | Role |
| --- | --- | --- |
| Framebuffer | `kernel/gfx/graphics.c` | Limine pixels. Always available. |
| VirtIO-GPU 2D | `kernel/gfx/vgpu.c` | Scanout resource, partial transfer, flush. |
| Software 3D | `kernel/gfx/tri.c`, `tile.c`, `scene.c` | CPU rasterizer and the path Mine Chris uses today. |
| VirGL 3D | `kernel/gfx/virgl_demo.c`, `virgl_cmd.c` | Minimal native client. Proof scene at boot when VirGL is negotiated. |

The proof scene is a boot test, not a second public API. It builds a VirGL
command stream (clear, triangle, depth, cube, textured cube) and checks
pixels with `TRANSFER_FROM_HOST_3D`. It then puts the 3D color resource on
scanout 0, flushes it, and restores the 2D primary so the desktop can draw.

## What is still one path

The window manager composites by drawing into the front buffer and flushing
a dirty rectangle. It is not a surface graph yet. Mine Chris does not submit
VirGL draws. A future scene translator can sit under `scene_draw` without
putting protocol constants into `GAMES/MINE`.

## Selection

`gfx.backend=framebuffer` skips the VirtIO probe.
`gfx.3d=software` keeps the CPU rasterizer even if VirGL was negotiated.
`gfx.3d=virgl` requires the feature. If it is missing, the guest logs the
reason and uses software.
`gfx.3d=auto` (the default) uses VirGL only when the feature was negotiated
and the proof init succeeds.
