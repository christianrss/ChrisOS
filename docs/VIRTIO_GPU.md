# VirtIO-GPU

The kernel driver is `kernel/gfx/vgpu.c`. Command bytes are built in
`kernel/gfx/virtgpu_enc.c`. The split virtqueue is `kernel/gfx/virtq.c`.
Resource and context ids live in `kernel/gfx/gpures.c`.

PCI id is vendor `0x1AF4`, device `0x1050`. Queue 0 is `controlq`. Queue 1
is `cursorq` when the device reports at least two queues.

## Features

The driver stores device, requested, and negotiated words.

Requested only when the device offers them and this tree implements the
contract:

- `VIRTIO_F_VERSION_1` (feature bit 32)
- `VIRTIO_GPU_F_VIRGL` (bit 0), when 3D is not forced off
- `VIRTIO_GPU_F_CONTEXT_INIT` (bit 4), only together with VirGL

Not requested: `VIRTIO_GPU_F_EDID`, `VIRTIO_GPU_F_RESOURCE_UUID`,
`VIRTIO_GPU_F_RESOURCE_BLOB`.

If `FEATURES_OK` does not stick with VirGL, the driver retries version 1
only and continues in 2D.

## Commands

Control commands use a 24-byte header, a fence id, and a response buffer.
The driver checks that the response echoes the fence. A device error or an
empty response is logged with the operation name, command, resource,
context, fence, and response type.

Implemented:

- `GET_DISPLAY_INFO`
- `RESOURCE_CREATE_2D`, `RESOURCE_ATTACH_BACKING`, `RESOURCE_DETACH_BACKING`,
  `RESOURCE_UNREF`
- `TRANSFER_TO_HOST_2D`, `RESOURCE_FLUSH`, `SET_SCANOUT` (rectangles, not
  only full frames)
- `GET_CAPSET_INFO`, `GET_CAPSET`
- `CTX_CREATE`, `CTX_DESTROY`, `CTX_ATTACH_RESOURCE`, `CTX_DETACH_RESOURCE`
- `RESOURCE_CREATE_3D`, `TRANSFER_TO_HOST_3D`, `TRANSFER_FROM_HOST_3D`
- `SUBMIT_3D`
- `UPDATE_CURSOR`, `MOVE_CURSOR` on `cursorq`

Resource ids come from a pool. Id 1 is not hard-coded. A failed create
releases the id. `vgpu_res_drop` unrefs and frees guest backing unless that
backing is the scanout, the command buffer, or a queue.

`gfx.stress` runs 1000 2D cycles and checks that live resource ids and free
descriptors return to the starting counts. Without that token the boot path
runs 4 cycles so the short QEMU gates still finish.

## Waiting

Each kick writes descriptors, an `mfence`, the available index, another
`mfence`, then the notify register. The used index is read with an `mfence`
before the descriptor id is consumed.

Completion polls the used ring. The budget is an `rdtsc` window (about a
second for 2D, longer for context and 3D) plus a spin cap. `pause` runs
during the wait so the host thread that retires VirGL fences can run.
Polling is the boot and fallback path. A PCI IRQ handler acks the ISR when
the line is below 16, but the IOAPIC redirect is not implemented, so the
handler is not the completion architecture.

A timeout marks the device dead and does not reuse the in-flight
descriptors.

## Display

`GET_DISPLAY_INFO` is logged (`scanout0` size and enabled bit, plus
`num_scanouts`). The desktop scanout stays at the Limine framebuffer size
because that is the buffer the window manager paints. Both sizes are
printed. Only scanout 0 is programmed.

## Host

```bash
qemu-system-x86_64 --version
qemu-system-x86_64 -device help
qemu-system-x86_64 -display help
```

`make run` keeps `-device virtio-gpu-pci` and does not enable GL.
`make test-qemu-gpu` is the 2D gate.
