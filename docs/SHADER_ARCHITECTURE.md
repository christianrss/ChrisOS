# ChrisOS shader architecture

This is the ChrisOS GLSL subset. It is not a GLSL 3.30 implementation, not Mesa, and not NIR.

```text
GLSL source
    |
    v
lexer -> parser -> AST -> semantic analysis
    |
    v
CSIR (Chris Shader IR)
    |
    +------------------+
    |                  |
    v                  v
TGSI emitter       CSIR interpreter
    |                  |
    v                  v
VirGL SUBMIT_3D    software rasterizer
    |
    v
virglrenderer -> host OpenGL
```

The public ChrisC calls are `shader_make`, `shader_ok`, `shader_log`, `shader_drop`, `prog_make`, `prog_attach`, `prog_link`, `prog_ok`, `prog_log`, `prog_drop`, `prog_uniloc`, `prog_setf`, `prog_samp`, `shader_vert`, and `shader_frag`. Those names do not mention TGSI or VirGL.

## Where the code lives

`kernel/gfx/shader/` is freestanding C compiled with the same SSE flags as the rest of the 3D code. The kernel demo and the host test share those sources. `-DSH_HOST` swaps `kmalloc` for `malloc`.

| File | Role |
| --- | --- |
| `sh_lex.c` | Tokens with line, column, and offset. `//` and `/* */`. `#version` 110–330. Other `#` directives are errors. |
| `sh_parse.c` | Recursive descent. Nesting stops at 32. |
| `sh_sem.c` | Scopes, types, builtins, constant folding, IR emission. |
| `sh_ir.c` | Verifier, dead-temporary elimination, text dump. |
| `sh_tgsi.c` | TGSI text from verified CSIR only. |
| `sh_exec.c` | Interpreter. |
| `sh_api.c` | Shader and program objects, linker, in-memory cache, `.CSI` blobs, guest handles. |

`tools/cshader` prints the AST, CSIR, or TGSI for one file. `tools/test_shader.c` is `host-shader-test`.

## CSIR

CSIR is typed and independent of TGSI. Temps are fixed slots, not a full SSA form, so both sides of `if`/`else` write the same temp and the interpreter skips the other side.

Matrices are column-major, matching GLSL: `M * v` is `col0*x + col1*y + col2*z + col3*w`. The CPU uniform upload uses that same layout. A `mat4` uniform occupies four vec4 slots. A `sampler2D` occupies a sampler slot, not the constant buffer.

Varyings are matched by name at link time. A missing fragment input or a type mismatch fails the link. Fragment `out vec4` maps to `COLOR0`. User varyings become `GENERIC` with perspective interpolation. `smooth` is accepted and means that default. `flat` and `noperspective` are rejected.

`for` loops unroll only when the bound is a compile-time constant and the trip count is at most 8. Anything else is an error. User functions are inlined when they end with `return`. Recursion is rejected.

## VirGL

`virgl_demo.c` compiles the files under `kernel/gfx/shader/glsl/` and submits each shader object in its own command buffer. Uniforms are uploaded with `SET_CONSTANT_BUFFER` after link. The shader is not rebuilt when a matrix changes.

The triangle, depth, colored cube, textured cube, RGB varying, and Lambert lighting draws all use that TGSI. There is no demo-specific TGSI string left in the renderer.

## Software backend

`sh_soft_vs` and `sh_soft_fs` run the same CSIR. `sh_soft_triangle` rasterizes one triangle with perspective-correct varyings into a buffer of at most 128x128. The older `tri.c` rasterizer is unchanged and does not execute CSIR.

## ChrisC

Syscall numbers 260–274. Handles belong to the language slot. `sh_guest_drop_owner` runs from `clvm_sys_close_slot`. A slot cannot drop another slot's shader. `prog_setf` is not marked `ret_float`, so only the value argument is read as a float.

`GAMES/SHADER/LAB.CC` is a source example. It is not on the boot disk and it does not draw a mesh. ChrisEditor has no syntax table for `.vert` or `.frag`. Highlighting is not wired. A failed relink keeps the previous TGSI and appends `kept previous program`.

## Mine Chris

`world.vert` and `world.frag` are compiled and linked during the VirGL boot (`PASS: shader mine link`). The voxel renderer in `GAMES/MINE/MINE.CC` still calls the software scene path. There is no `#ifdef VIRGL` in that game. Drawing those voxels through VirGL is unimplemented.

## Limits

Source 4096 bytes, 768 tokens, 512 AST nodes, 64 symbols, 384 IR instructions, 96 temps, 160 immediates, 8 errors, TGSI text 3600 bytes. The in-memory cache holds 8 successful compiles. It is not counted in `sh_live_count`.

## Offline blob

`.CSI` starts with magic `0x52495343`, version 1, stage, instruction count, and a checksum of the IR bytes. A short or corrupt blob is rejected.

## What is not claimed

Geometry, tessellation, and compute shaders are rejected. `discard` and `sin` are implemented and covered by the host interpreter. They are not part of the QEMU pixel gate. Images from the software interpreter and VirGL are not required to match bit for bit.
