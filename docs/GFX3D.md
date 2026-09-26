# 3D graphics

## Software

The CPU renderer remains the fallback and the reference path:

- matrices in `kernel/gfx/math3d.c` (row-major storage, column vectors, `M * v`)
- contexts in `kernel/gfx/gfx3d_ctx.c`
- meshes, textures, triangles, tiles, and a depth buffer under `kernel/gfx/`
- scene nodes in `kernel/gfx/scene.c`

Mine Chris (`GAMES/MINE`) still calls the software scene. It does not
include VirGL opcodes. `world()` and `meshf()` are not on the shared
backend yet. Until that dispatch lands, Mine Chris runs on the software
renderer even when `3D backend -> virgl` was printed for the boot proof.

Host tests that already covered math and contexts were not replaced.
Tile binning still must not let two jobs write the same tile's color and
depth. That race was not reworked in this round.

## Matrix ABI

`Mat4f` stores 16 floats row-major and multiplies column vectors (`M * v`).
Translation lives at `m[3]`, `m[7]`, `m[11]`. `mat4f_mul(O, A, B)` is
`A * B` (apply B, then A). GLSL and CSIR `mat4` are column-major, so
`mat4f_to_glsl` transposes before `sh_uniform_set` / VirGL constant upload.
Games do not call `transpose`. The boot proof still uploads its historical
hand-built uniforms directly; that path is not a `Mat4f`.

A translation of `(4, -2, 1.5)` applied to `(1, 2, 3, 1)` is the same clip
position on the CPU and in `mvp.vert` after `mat4f_to_glsl`. Host gate:
`host-gfx3d-abi-test`.

## VirGL backend

`kernel/gfx/gfx3d.c` is the backend-neutral API (context, target, mesh,
texture, program, draw). `kernel/gfx/gfx3d_virgl.c` is the VirGL device:
separate VirGL object handles, a shared DMA slab for buffers and textures,
dedicated color-target backing, depth without guest backing, cached blend /
DSA / rasterizer / sampler / vertex-element / surface objects, and command
batches that flush and continue at `VIRGL_CMD_MAX`.

`virgl_demo.c` is only the boot gate. It creates those objects through
`gfx3d_*` and checks the same pixels as before. `gfx.3d=virgl` does not
switch to software when the device is missing. `gfx.3d=auto` may.

Readback is `TRANSFER_FROM_HOST_3D` into the window surface. That is GPU
render plus CPU readback, not the final scanout path. `gfx3d_present_scanout`
can point the display at a target; `gfx3d_scanout_primary` restores the
desktop.

There is no measured FPS comparison in this round. No performance claim
is made for software versus VirGL.

## Status

| Piece | Status |
| --- | --- |
| Software rasterizer | HOST_TESTED. Still the Mine Chris path. |
| Gfx3D API, VirGL object lifecycle, persistent mesh/texture, matrix ABI | HOST_TESTED (`host-gfx3d-abi-test`). Not re-run under QEMU in this change. |
| VirGL clear, triangle, depth, cube, textured cube, scanout | Previously QEMU_TESTED. The proof now calls the shared API; QEMU was not available here, so this revision stays HOST_TESTED until `test-qemu-virgl` runs. |
| Mine Chris on VirGL | UNIMPLEMENTED |
| Benchmarks | not measured |
| Physical GPU | HARDWARE_TESTED not claimed |
