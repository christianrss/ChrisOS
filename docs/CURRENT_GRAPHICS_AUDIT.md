# Current graphics audit

Status words used here: `UNIMPLEMENTED`, `EXPERIMENTAL`, `HOST_TESTED`,
`QEMU_TESTED`, `HARDWARE_TESTED`, `STABLE`.

VirtIO-GPU/VirGL is a paravirtual device. It is not an Intel, AMD, or
NVIDIA GPU driver. Nothing in this tree is `HARDWARE_TESTED` or `STABLE`.

| Path | Status | What was actually run |
| --- | --- | --- |
| Software framebuffer | QEMU_TESTED earlier, still the desktop path | Window manager draws with `gfx_*`. `make run` keeps this path when the VirtIO device has no GL. |
| VirtIO-GPU 2D | QEMU_TESTED for scanout and the resource cycle | `test-qemu-gpu` expects `virtio-gpu ready`. With `gfx.stress`, the guest runs 1000 create/attach/transfer/flush/detach/unref cycles and prints `PASS: vgpu resource stress 1000`. |
| Software 3D | HOST_TESTED earlier | Rasterizer, tiles, and Mine Chris still draw on the CPU. This round did not re-prove perspective-correct UVs or tile races. |
| VirGL transport | QEMU_TESTED for one proof scene. Not STABLE | On QEMU 8.2.2 with `virtio-vga-gl` and `gtk,gl=on` (host GL was llvmpipe), the guest log contained `VIRGL feature: yes`, the clear/triangle/depth/cube/texture/present lines, and `3D backend -> virgl`. |
| Hardware cursor | EXPERIMENTAL | `UPDATE_CURSOR` is submitted on `cursorq`. The boot log can print `virtio-gpu cursorq`. There is no pixel gate for the cursor image. Software cursor remains the fallback. |
| IRQ completion | EXPERIMENTAL | The PCI line is registered. Completion still polls the used ring. IOAPIC redirect is still a stub, so this is not an interrupt-driven architecture. |
| GLSL frontend and CSIR | HOST_TESTED | `make host-shader-test` covers the lexer, parser, type checker, verifier, TGSI text, interpreter, link errors, CSI blobs, and a 1000-compile live-object check. This is a ChrisOS GLSL subset, not GLSL 3.30. | 
| TGSI / VirGL shader backend | QEMU_TESTED for the proof shaders. Not STABLE | The triangle, depth, cube, textured cube, RGB varying, and Lambert lighting draws use TGSI from the compiler. The same boot prints `PASS: glsl compile`, `PASS: virgl varying`, `PASS: virgl lighting`, `PASS: virgl shader switch`, and `PASS: shader mine link`. | 
| Software CSIR interpreter | HOST_TESTED | `sh_soft_vs`, `sh_soft_fs`, and `sh_soft_triangle` run the same CSIR. The older `tri.c` rasterizer does not. | 
| Mine Chris voxels on VirGL | UNIMPLEMENTED | `world.vert` and `world.frag` are compiled and linked on the VirGL boot. `GAMES/MINE/MINE.CC` still draws through the software scene path. There is no `#ifdef VIRGL` in the game. |

Host unit gates: `host-virtq-test`, `host-gpures-test`, `host-virgl-cmd-test`.
QEMU 2D gate: `test-qemu-gpu` (not a VirGL pass).
QEMU VirGL gate: `test-qemu-virgl`. A host without a GL frontend prints
`SKIP: host QEMU lacks VirGL support` and exits 2. That skip is not a pass.

Boot tokens, parsed by the existing cmdline parser:

```text
gfx.backend=framebuffer
gfx.backend=virtio
gfx.3d=auto
gfx.3d=software
gfx.3d=virgl
gfx.stress
gfx.virgl.debug
```

`auto` requests VirGL when the device offers `VIRTIO_GPU_F_VIRGL` and the
contract in this tree can run. If init fails, the guest prints `VirGL failure`
and `3D backend -> software` and continues. It does not panic.

`EDID`, `RESOURCE_UUID`, and `RESOURCE_BLOB` are detected and not requested.
This QEMU 8.2 build did not offer `VIRTIO_GPU_F_CONTEXT_INIT`. The guest
uses the legacy `CTX_CREATE` path in that case.
