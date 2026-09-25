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
| VirGL | QEMU_TESTED for one proof scene. Not STABLE | On QEMU 8.2.2 with `virtio-vga-gl` and `gtk,gl=on` (host GL was llvmpipe), the guest log contained `VIRGL feature: yes`, `PASS: virgl clear`, `PASS: virgl triangle`, `PASS: virgl depth`, `PASS: virgl cube`, `PASS: virgl textured cube`, `PASS: virgl present`, and `3D backend -> virgl`. |
| Hardware cursor | EXPERIMENTAL | `UPDATE_CURSOR` is submitted on `cursorq`. The boot log can print `virtio-gpu cursorq`. There is no pixel gate for the cursor image. Software cursor remains the fallback. |
| IRQ completion | EXPERIMENTAL | The PCI line is registered. Completion still polls the used ring. IOAPIC redirect is still a stub, so this is not an interrupt-driven architecture. |
| Mine Chris on VirGL | UNIMPLEMENTED | The game still calls the software scene/voxel path. The VirGL client is not a voxel translator. |

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
