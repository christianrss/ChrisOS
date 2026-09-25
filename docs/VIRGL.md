# VirGL

VirGL here is a paravirtual 3D context on VirtIO-GPU. The guest sends a
command stream. QEMU's `virtio-vga-gl` / `virtio-gpu-gl` device passes it
to `virglrenderer`, which calls host OpenGL. This is not a driver for a
physical GPU.

The client is native and small. Mesa is not linked into ChrisOS. Opcodes
and object layouts match virglrenderer 1.0.0 and the Mesa virgl encoder
(`VIRGL_CMD0(cmd, obj, len)`, length in dwords after the header dword).

## Transport

```text
feature VIRTIO_GPU_F_VIRGL
        |
        v
GET_CAPSET_INFO / GET_CAPSET
        |
        v
CTX_CREATE / CTX_DESTROY
        |
        v
RESOURCE_CREATE_3D + CTX_ATTACH_RESOURCE
        |
        v
Virgl command buffer
        |
        v
SUBMIT_3D + fence
        |
        v
TRANSFER_FROM_HOST_3D (readback) or SET_SCANOUT + RESOURCE_FLUSH
```

Boot prints:

```text
VirtIO GPU detected
VIRGL feature: yes/no
CONTEXT_INIT: yes/no
number of capsets: N
3D backend -> virgl
```

or `3D backend -> software` when the feature is absent or init fails.

Capset ids accepted are `VIRTIO_GPU_CAPSET_VIRGL` (1) and
`VIRTIO_GPU_CAPSET_VIRGL2` (2). The blob is copied only when the device
size fits the fixed buffer (8192 bytes). A larger blob is rejected.
VIRGL2 is preferred when that capset is present. QEMU 8.2.2's virgl
command handler calls `virgl_renderer_context_create`, which selects the
VIRGL2 capset inside virglrenderer 1.0.0. This QEMU did not negotiate
`VIRTIO_GPU_F_CONTEXT_INIT`, so `context_init` stays 0.

## Command buffer

`kernel/gfx/virgl_cmd.c` refuses a zero-capacity buffer, a handle of 0,
and a write past the dword capacity. Object handles start at `0x1000`.
Shaders are TGSI text, not GLSL. The proof scene creates blend, depth,
rasterizer, sampler, and shader objects once and rebinds them.

The proof scene in `kernel/gfx/virgl_demo.c` is executed by the real
device:

1. Repeated context create/destroy (64 with `gfx.stress`, else 4).
2. Repeated 1x1 `RESOURCE_CREATE_3D` / attach / detach / unref (128 or 4).
3. 128x128 color target, clear green, readback. `PASS: virgl clear` only
   if at least 80% of pixels are green.
4. Red triangle on the remaining green. `PASS: virgl triangle`.
5. Two triangles, depth `LESS`. `PASS: virgl depth`.
6. Indexed cube with back-face culling. `PASS: virgl cube`.
7. Textured cube, nearest 4x4 checker, after a depth clear. `PASS: virgl
   textured cube` only if both red and blue texels are visible.
8. `SET_SCANOUT` of that color resource and `RESOURCE_FLUSH`, then the 2D
   scanout is restored. `PASS: virgl present`.

The triangle, cube, and texture are command-stream draws. They are not
rasterized on the CPU and copied into the resource.

`gfx.virgl.debug` logs context create, resource create, attach, submit
dword count, and fence completion. It does not log every dword.

## QEMU

Requirements on the host:

- QEMU built with `virtio-vga-gl` or `virtio-gpu-gl`
- `virglrenderer`
- a display frontend that can create a GL context (`egl-headless` when a
  DRM render node exists, otherwise `gtk,gl=on` with `libEGL` and `DISPLAY`)
- host OpenGL. A llvmpipe renderer still goes through virglrenderer. That
  is a QEMU test, not a hardware test.

```bash
qemu-system-x86_64 --version
qemu-system-x86_64 -device help | grep -E 'virtio-vga-gl|virtio-gpu-gl'
qemu-system-x86_64 -display help
```

```text
make run          # virtio-gpu-pci, no GL, software 3D if VirGL is absent
make run-virgl    # gtk,gl=on + virtio-vga-gl
make test-qemu-virgl
```

`test-qemu-virgl` builds `build/os-virgl.iso` with
`gfx.3d=virgl gfx.stress gfx.virgl.debug`. If the host has neither a DRM
node for `egl-headless` nor GTK GL, the target prints
`SKIP: host QEMU lacks VirGL support` and exits 2. The skip is not a pass.
The target is not part of `qemu-gates`.

On the machine that produced the serial log, `egl-headless` failed with
`no drm render node`. The gate used `-display gtk,gl=on -device virtio-vga-gl`.
`glxinfo` reported Mesa llvmpipe. The guest still received
`VIRGL feature: yes` and the pixel passes above.

## Not in this client

No OpenGL entry points. No Intel/AMD/NVIDIA registers. No full Mine Chris
scene. Context loss is a failed submit: the device is marked dead and the
desktop keeps the software path. There is no automatic replay of a lost
context.
