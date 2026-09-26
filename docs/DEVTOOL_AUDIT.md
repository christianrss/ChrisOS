# Developer toolkit audit

Audit date: 2026-09-24.

Requested base: `feat/os2` at `dc662538477d7322312a2d54c983207b5b6259d7`
("Stability campaign: CFS lock, 3D contexts, driver timeouts, QEMU gates (#3)").

`origin/feat/os2` was still `dc662538` when this campaign started. The work
tree was `cursor/foundation-campaign-7c6f` at `1e81ca46d41c136f2a842d0a43ee8d1fee0b4268`,
six commits ahead of that base (installer, CFS read-ahead, ATA DMA reuse,
TLB shootdown IPI, ChrisC `memmove` pointer width, foundation audit). Those
commits are kept. Nothing was reset.

## Edit, compile, link, run

ChrisC apps:

1. `APPS/EDITOR/EDITOR.CC` edits one buffer (`g_buf`, up to 256 KiB).
2. `:cc` calls `sys_cc` (`SYS` 100).
3. `kernel/lang/clvm_sys.c` forwards that to `lang_compile_path`.
4. `compiler/chrisc/chrisc.c`, linked into the kernel, compiles the source.
5. `lang_write_map` writes a text `.MAP`. The same path now also writes `.CDBG`.
6. The image is a `.CLV`. `sys_run` (`SYS` 101) starts a CLVM slot.
7. `:make` calls `sys_make`, which runs `kernel/tools/chrismake.c` inside the OS.

There is no separate link step for ChrisC. The compiler emits CLVM bytecode
directly. Native ELF linking is a different pipeline: KCC -> ChrisAsm ->
ChrisO -> ChrisLd.

`APPS/CC/CC.CC` is a second, much smaller compiler. It is not the compiler
behind `sys_cc`. See `docs/CHRISC_SELFHOST_CONFORMANCE.md`.

## Debug flow

`kernel/tools/editor_window.c` is the native rescue editor.

- F7 toggles a PC breakpoint for the current line via `lang_bp_toggle_line`.
  That needs a `.MAP` already loaded in a live slot.
- F9 sets `g_want_debug` and runs the program.
- F10 steps with `lang_debug_step` while paused, otherwise continues.
- The first slot with `debug_on` answers `lang_debug_pc`, `lang_debug_line`,
  `lang_debug_fn`, and `lang_debug_fault`.
- One watch exists: `lang_debug_set_watch` stores a single address on the slot.
- Faults are also recorded globally with `proc_record_fault`.

`APPS/EDITOR` can compile and run. It cannot create a debug session, plant a
breakpoint before launch, or read frames, locals, or watches.

## .MAP format

Text records, previously:

```
0xPC line
F name pc
```

`ChrisMapEnt` already had `file_id`, and `lang_write_map` dropped it.
`gen_stmt` also stored `file_id = 0` for every statement. The include line
map inside the compiler (`LineMap`) was only used for diagnostics.

The writer now emits `0xPC line file_id`. The loader accepts the old form and
stores a missing file id as 0. `.MAP` is still written.

## Information the compiler used to discard

Still discarded, even after this increment:

- column of each statement (CDBG column is 1)
- scope table, locals, arguments, types, and location lists
- a symbol index (functions, fields, typedefs, references)

Now kept:

- `file_id` on each `ChrisMapEnt`
- file paths, up to `CHRIS_FILE_MAX` (32), on `ChrisResult`
- function export name, start PC, and argc (already present)
- up to `CHRIS_DIAG_MAX` (8) structured diagnostics

## Global APIs and ownership

Public debugger state is still process-global:

- `g_want_debug`
- `lang_last_error()` (one string)
- `proc_last_fault()`
- the first `debug_on` slot

`DebugSession` now exists in `compiler/debug/dbg_session.c` with an owner id.
A caller who does not own the session cannot add breakpoints, watches, or
faults. The kernel tick does not yet route every debug command through that
table. `lang_debug_step_over` and `lang_debug_step_out` do use the shared
step policy on the slot.

CLVM slot ownership from the foundation campaign is unchanged.

## Native editor versus ChrisC apps

Native (`kernel/tools/editor*.c`): breakpoints, step, continue, watch address,
call PCs, fault registers, compile, JIT run.

ChrisC (`APPS/EDITOR`): buffer, search, command line, `sys_cc`, `sys_run`,
`sys_make`, `sys_err`. No debugger, no build events, no diagnostic list.

## compiler/chrisc versus APPS/CC

`compiler/chrisc` is the bootstrap compiler. It has a preprocessor, includes,
typedefs, structs, floats, unions, casts, multi-file compile, diagnostics,
and source maps.

`APPS/CC` is a ChrisC program with a small lexer and codegen. A search of
`APPS/CC/CC.CC` finds no `float`, `typedef`, `#include`, or `union`. It does
not emit `.MAP` or `.CDBG`. Compiling through `sys_cc` is not self-hosting:
that path runs the kernel-linked bootstrap compiler.

## Host-side self-host dependencies

The host still needs:

- GCC for the kernel and for `compiler/chrisc`
- Python for `tools/qemu_gate.py` and asset generation
- Limine (`BOOTX64.EFI`) as the bootloader
- QEMU for the boot gates

ChrisMake inside the OS can rebuild ChrisC apps only by calling back into
`compiler/chrisc`. KCC/ChrisAsm/ChrisLd do not compile the kernel sources.

## Static limits

| Site | Limit | On overflow |
| --- | --- | --- |
| `APPS/EDITOR` `g_path`, `g_dir` | was 96, now 512 (`FS_PATH`) | copy stops and writes the NUL inside the buffer |
| `g_status` | 80 | same bounded copy |
| `g_find` | 64 | same |
| `g_line_starts`, `g_line_states` | was 2048, now 8192 | extra lines are not indexed; the arrays are not written past the end |
| `g_buf` | 256 KiB, then 64 KiB, then 8 KiB | malloc fallback |
| `CHRIS_MAP_MAX` | 8192 | further map entries are dropped |
| `CHRIS_DIAG_MAX` | 8 | later diagnostics are dropped; `ChrisDiag` still holds the latest error |
| `CHRIS_FILE_MAX` | 32 paths | further paths are omitted; line `file_id` values remain |
| `LangSlot` breakpoints | 32 PCs | toggle fails |
| `LangSlot` function names | 16 names of 24 bytes | extra `F` records are skipped |
| debug sessions | 8 | `dbg_session_create` returns -1 |
| session breakpoints | 32 | add returns -1 |
| session watches | 8 | add returns -1 |
| in-memory `CdbgImage` | 32 files, 256 lines, 64 funcs | `cdbg_decode` fails; `cdbg_from_result` can still write a larger line table |

## Bugs found in this audit

`EDITOR-PATH-01`, memory corruption. `path_set` and `dir_set` kept incrementing
`i` after the copy cap and wrote the NUL at that index. A 200-byte path
wrote past `g_path[96]`. `str_set` had no cap at all, so a 160-byte `g_err`
could be copied into `g_status[80]`, and `g_cmd[80]` into `g_find[64]`.
`join_path` wrote an unbounded string into `full[160]`. `do_go` copied
`g_path` into `out[96]`.

`MAP-FILE-01`. `file_id` was thrown away at codegen and again at `.MAP` write.

`DBG-GLOBAL-01`. One global "whoever is in debug" slot, one watch, breakpoints
only after a map is loaded, step-line only.

`DIAG-ONE-01`. `fail()` kept a single `ChrisDiag`.

`CC-GAP-01`. `APPS/CC` is not equivalent to `compiler/chrisc`.

## Duplication

Two editors, two compilers, two ways to compile (native editor and `sys_cc`),
and a text `.MAP` parser that reimplemented hex decoding inside
`lang_load_map`. The map line parser now lives in `cdbg_parse_map_line`.

## What this increment changed

- Bounded copies in `APPS/EDITOR/EDITOR.CC`, path buffers raised to 512, line
  index raised to 8192.
- Host text model in `compiler/edit/editmodel.c` (gap buffer, undo, redo,
  four buffers, dirty close, search, replace, go to line). The ChrisC editor
  does not call it yet.
- `.MAP` keeps `file_id`. `.CDBG` is written next to it.
- `DebugSession` plus step in, step over, and step out policy. The kernel
  tick uses that policy. Session breakpoints are tested on the host and are
  not yet the breakpoints the native editor toggles.
- `ChrisDiagnostic` array, with `ChrisDiag` kept for `lang_last_error()`.

## Gates run while landing this increment

`make kernel` linked, including `compiler/debug/cdbg.o` and
`compiler/debug/dbg_session.o`.

`make host-gates` was run with `-k`. These new tests passed:
`test_editor_path`, `test_cdbg`, `test_dbg_step`, `test_editmodel`.
`test_editor_vi` needed `kernel/metal/spin.c` on the host link line.
`host-kthread-smp-test` and `host-job-saturate-test` needed an empty
`apic_enable_local` on the host, matching the foundation change that calls
it from `job.c`. `test_chrisc_apps` still compiles `APPS/EDITOR`.

These host-gates targets failed on this machine and also fail without the
toolkit edits:

- `host-jit-bench-test` wants a 5x JIT speedup. The same binary built from
  the unmodified compiler reports interp 0.013s, jit 0.210s, speedup 0.06x.
- `host-doom-jit-entry-test` returns 3, "buffer too small for table", after
  the JIT falls back (`used=18` against a 1.2 MB image).
- `test_gfx2d` does not compile: `tools/test_gfx2d.c` includes
  `../kernel/gfx2d.h`, and that header is `kernel/gfx/gfx2d.h`. Linking
  `gfx2d.c` also wants `zbuf_set_size`. Left unchanged.

`qemu-devtool-smoke` does not exist yet. No self-host level above SH0 moved.

## Not done

IDE layout, Problems panel, build events, test runner, symbol index, locals,
expression watches, Dev API syscalls, self-hosted compiler stages, native
toolchain expansion, internal kernel link, reproducibility gate, and
`qemu-devtool-smoke`.
