# Current graphics audit

| Path | Class | Host | QEMU | Hardware | Limits | Gate |
| --- | --- | --- | --- | --- | --- | --- |
| Software framebuffer | IMPLEMENTED | `test_gfx2d` exists | earlier desktop boot was not repeated | no | Window manager draws through `gfx_*`. Pitch and pixel format are whatever Limine published. | `test_gfx2d` |
| VirtIO-GPU | EXPERIMENTAL | no resource-lifecycle host test | `test-qemu-gpu` exists and was not re-run | no | `virtio_gpu_boot` in `kernel/gfx/hwgate.c` probes and can scan out. There is no audited create, attach, partial flush, resize, unref, and destroy cycle with a leak check. | `test-qemu-gpu` |
| Software 3D | HOST-TESTED earlier | math and context tests exist | not a GPU | no | This is a software renderer into a 2D surface. It is not GPU acceleration. | `host-gfx3d-ctx-test` |
| VirGL | UNSUPPORTED | no | no | no | Not in this milestone. | none |
| Cursor plane | UNSUPPORTED as a VirtIO cursor | software cursor only if the desktop draws one | no | no | No `UPDATE_CURSOR` gate. | none |

Desktop, ChrisC, and games should keep using the common draw path.
They should not grow a direct VirtIO-GPU dependency until the resource
owner, dirty rectangles, and destroy path exist and a gate shows a
thousand create/destroy cycles without a leftover id.
