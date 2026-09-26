# Developer toolkit architecture

The toolkit stays one stack. `compiler/chrisc` remains the bootstrap
compiler. `APPS/EDITOR` remains the program that becomes ChrisIDE.
`kernel/tools/editor*` stays the rescue editor. `APPS/CC` is a conformance
subject, not a second backend.

```
APPS/EDITOR  (ChrisC, future ChrisIDE)
    |  Dev API, not string scraping
    v
debug sessions, diagnostics, build events, CDBG, symbol queries
    |
    +-- compiler/chrisc     bootstrap compiler, emits bytecode + CDBG
    +-- compiler/clvm       interpreter used by the debugger
    +-- compiler/jit        run path, not the first debugger
    +-- kernel/tools/chrismake
    +-- kernel/tools/chrisbuild
    +-- compiler/chrisasm, chrisld, kcc    native toolchain, still minimal
```

`sys_cc` calling `compiler/chrisc` inside the kernel is the bootstrap path.
Documents and gates must call it that. It is not self-host level SH2.

## Debug information

`.MAP` stays. Each record is `0xPC line file_id`. Old files without
`file_id` still load.

`.CDBG` version 1 is a little-endian binary:

- magic `CDBG`, version, compiler ABI, flags
- FNV-1a checksum of the bytecode when the bytes are available, else 0
- source hash (0 until the compiler feeds one)
- file table: id, path
- line table: `pc_start`, `pc_end`, file id, line, column
- function table: name, start PC, end PC, file, line, argc

`cdbg_line_at` answers "which line owns this PC?" by the last entry with
`pc_start <= pc`. `cdbg_func_at` answers "which function?" with a half-open
`[start, end)` range. `cdbg_file_at` uses the line's file id.

Column, scopes, locals, and types are not in version 1. Readers must reject
a different version.

## Debug sessions

```
DebugSession
  id, owner, vm slot
  state: CREATED RUNNING PAUSED WAITING HALTED FAULTED STOPPED
  breakpoints: file id + line, optional temporary flag, resolved PC
  watches: address + kind (memory, int, float, pointer)
  fault_type, fault_pc
```

`caller == -1` is the kernel. Any other caller must be the owner. This is
the rule the Dev API has to keep when it is exposed to ChrisC: one app does
not drive another app's session.

Source breakpoints are stored before launch. `dbg_session_resolve` binds
them to PCs from the line table. Two files can both have line 4; the file
id selects the PC. A temporary breakpoint is removed when it hits.

Stepping is a pure function, `dbg_step_should_pause(mode, start_line,
start_depth, line, depth)`:

- step in pauses when the source line changes
- step over pauses when the line changes and the call depth is back at or
  below the start depth
- step out pauses when the call depth drops below the start depth

`lang_debug_step`, `lang_debug_step_over`, and `lang_debug_step_out` arm
that policy on the debug slot. Depth is the CLVM call stack (`csp`). The
interpreter is the stepping engine. JIT debugging is out of this design.

The native editor still uses the slot PC breakpoint list. Moving F7 to
session breakpoints is later work, so both can exist during the transition.
New IDE code should call the session.

## Diagnostics

`ChrisDiag` remains the single string `lang_last_error()` already prints.
`ChrisResult.diags[]` holds up to 8 `ChrisDiagnostic` records with severity
(`ERROR`, `WARNING`, `NOTE`), code, file, line, column, end line, end
column, and message. The compiler stops at the first hard error today, so
most failed compiles still publish one record. The array is the IDE's
source. The Problems panel is not built yet.

## Text model

`compiler/edit/editmodel.c` is a gap buffer with grouped undo and redo,
four buffers, a dirty flag, search, replace, and go-to-line. Host tests
cover it. `APPS/EDITOR` still uses its own `g_buf` and does not link this
model, because the editor is ChrisC and the model is C. The next editor
change should either call a Dev API wrapped around this model or port the
same operations into ChrisC and test them the same way. A second editor
behavior must not appear.

The ChrisC editor's path copy is `copy_cap(dst, src, cap)`. The index used
for the NUL is always inside the destination. Paths use cap 512, matching
`FS_PATH`. The line index is 8192 entries and refuses to write past that.

## Build, tests, native tools

ChrisMake is unchanged. The IDE should later subscribe to build events
(`BUILD_STARTED`, `TARGET_STARTED`, `COMPILE_STARTED`, `DIAGNOSTIC`,
`ARTIFACT`, `TARGET_FINISHED`, `BUILD_FINISHED`) instead of parsing the
make log.

The test protocol (`TEST_STARTED`, `TEST_PASSED`, `TEST_FAILED`,
`TEST_SKIPPED`, `TEST_FINISHED`) is not implemented.

Native codegen stays KCC -> ChrisAsm -> ChrisO -> ChrisLd -> one ELF
`PT_LOAD`. The gap is `docs/NATIVE_TOOLCHAIN_GAP.md`.

## Compatibility

- CLVM images are unchanged.
- Old `.MAP` lines without a file id still parse.
- `lang_debug_step` is still step in.
- `lang_last_error()` still returns one diagnostic string.
- Existing shell commands and `sys_cc` / `sys_run` / `sys_make` numbers are
  unchanged.
