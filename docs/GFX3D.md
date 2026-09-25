# 3D graphics

## Software

The CPU renderer remains the fallback and the reference path:

- matrices in `kernel/gfx/math3d.c` (row-major, row vector on the left)
- contexts in `kernel/gfx/gfx3d_ctx.c`
- meshes, textures, triangles, tiles, and a depth buffer under `kernel/gfx/`
- scene nodes in `kernel/gfx/scene.c`

Mine Chris (`GAMES/MINE`) calls that scene and its own voxel drawing. It
does not include VirGL opcodes. This round did not move those draws onto
the VirGL command encoder. Doing that needs a mesh translator (chunks,
textures, depth) on top of the client that already submits a textured
cube. Until that exists, Mine Chris runs on the software renderer even
when `3D backend -> virgl` was printed for the boot proof.

Host tests that already covered math and contexts were not replaced.
Tile binning still must not let two jobs write the same tile's color and
depth. That race was not reworked in this round.

## VirGL

`gfx.3d=auto` uses the VirGL proof client when `VIRTIO_GPU_F_VIRGL` was
negotiated and the clear/triangle/cube sequence succeeds. Failure falls
back to software without a panic.

The proof client records:

- color and depth resources
- vertex and index buffers
- one nearest sampler and one 4x4 texture
- TGSI vertex and fragment shaders
- a perspective cube at a fixed angle

Readback is `TRANSFER_FROM_HOST_3D` into guest backing. Pass lines require
pixel counts, not a screenshot.

There is no measured FPS comparison in this round. No performance claim
is made for software versus VirGL.

## Status

| Piece | Status |
| --- | --- |
| Software rasterizer | HOST_TESTED earlier. Still the Mine Chris path. |
| VirGL clear, triangle, depth, cube, textured cube, scanout | QEMU_TESTED on QEMU 8.2.2 + virglrenderer 1.0.0 + llvmpipe via GTK GL |
| Mine Chris on VirGL | UNIMPLEMENTED |
| Benchmarks | not measured |
| Physical GPU | not in scope |
