# ChrisOS Stability Audit

Living tracker for the stabilization campaign. Baseline HEAD of `feat/os2`:
`2550b9cab06c3ee2e81a292691031935ec5613df` (confirmed current; no later commits).

Severity: **P0** memory/FS corruption, isolation break, arbitrary execution,
unpredictable panic, or broadly wrong behavior. **P1** core feature broken or
important race. **P2** robustness/recovery/leaks/edge cases. **P3**
quality/diagnostics/perf without integrity risk.

Status: `FIXED` (with regression test), `OPEN` (confirmed, not yet fixed),
`INVESTIGATING`.

---

## FIXED

### CHRISC-01 — `float *` parameter treated as a float scalar (P0)

- **Subsystem:** ChrisC compiler (argument passing / type coherence).
- **Symptom:** Functions taking `float *` (e.g. `LIB/SIM.CC` `sim_move`,
  Mine physics) produced wrong results; pointer arguments were corrupted.
- **Root cause:** In parameter parsing, `f->arg_float[argc] = dt.isf;` set the
  argument's *value* representation from the pointee type. A `float *` has
  `dt.isf == 1` and `dt.ptr == 1`, so the argument was flagged as a by-value
  float. The prologue/epilogue then used `FSTORE`/`FLOAD` (32-bit float move)
  to pass a 64-bit pointer, conflating pointer-ness with value float-ness.
- **Reproducer:** `tools/test_chrisc_ptr_float.c` case `move` (the campaign's
  minimal reproducer: `struct Actor a[1]` + `move(&a[0].x, &a[0].vx)`).
- **Files:** `compiler/chrisc/chrisc.c` (parameter parse, ~L6859).
- **Fix:** `arg_float[argc] = (dt.isf && !ptr)` — a pointer value is a 64-bit
  address, never a float, regardless of its pointee.
- **Regression test:** `test_chrisc_ptr_float` (in `host-gfx3d` / `host-gates`).
- **Commit:** see `fix(chrisc): pointer value vs pointee float coherence`.
- **Residual risk:** The symbol type model still overloads `is_float` on a
  pointer symbol to mean "pointee is float". A dedicated internal type
  descriptor would remove this ambiguity (tracked as CHRISC-TYPE-REFACTOR, P3).

### CHRISC-02 — `*p` (N_DEREF) of a float pointer not marked float (P0)

- **Subsystem:** ChrisC compiler (expression typing / codegen).
- **Symptom:** `*vx + 0.16` and similar produced garbage; the canonical
  reproducer returned `0.0` / NaN-like values.
- **Root cause:** The `N_DEREF` node created for unary `*` never set
  `is_float`. For a `float *`, `*p` was typed as int, so mixed float/int
  arithmetic (`gen_float_binop`) inserted a spurious `ITOF` that reinterpreted
  the loaded float bits as an integer. (In the VM `LOAD`≡`FLOAD` and
  `STORE`≡`FSTORE`, so the corruption was the `ITOF`, not the memory op.)
- **Reproducer:** `tools/test_chrisc_ptr_float.c` cases `move`, `multi`.
- **Files:** `compiler/chrisc/chrisc.c` (`unary()` N_DEREF construction; new
  helper `deref_pointee_float`).
- **Fix:** Set `N_DEREF.is_float` from the pointee's float-ness (symbol
  `is_float` for a pointer var, `CAST_PFL` for pointer casts, `&lvalue`).
- **Regression test:** `test_chrisc_ptr_float`.
- **Residual risk:** Same type-model ambiguity as CHRISC-01.

### CHRISC-03 — compound assignment dropped float-ness (P1)

- **Subsystem:** ChrisC compiler (compound assignment codegen).
- **Symptom:** `*p += 2.5`, `f += 2.5` produced integer arithmetic on float
  bits (`compound` case returned `0.0`).
- **Root cause:** Both compound-assignment sites build the binary op node by
  hand and never set `is_float` (the normal `binary()` path does). The operand
  `N_VAR` in the statement-level path also lacked `is_float`.
- **Reproducer:** `tools/test_chrisc_ptr_float.c` case `compound`.
- **Files:** `compiler/chrisc/chrisc.c` (assignment-expr and statement-level
  compound paths; new helper `value_is_float` keeps pointer arithmetic integer).
- **Fix:** Propagate `is_float` on the synthesized binary node using
  `value_is_float` (float scalar / float deref = float; pointer value = int).
- **Regression test:** `test_chrisc_ptr_float` (`compound`).

### MINE-01 — WASD movement / physics broken by float-pointer defects (P1)

- **Subsystem:** ChrisC + `LIB/SIM.CC` (Mine physics).
- **Symptom:** Movement did not integrate correctly (the campaign's Mine
  movement bug). `sim_move` takes six `float *` out-parameters and applies
  velocity integration with `* 0.72` damping — exactly the broken pattern.
- **Root cause:** CHRISC-01/02/03 (the defect is in the language, not the app;
  `LIB/SIM.CC` was **not** modified).
- **Reproducer / regression test:** `tools/test_chrisc_move.c` — holds
  "forward" N frames (finite, coherent forward motion), releases (velocity
  damps toward zero), verifies no cross-axis contamination and no NaN/Inf.
- **Files:** fixed via `compiler/chrisc/chrisc.c`; `LIB/SIM.CC` unchanged.
- **Regression test:** `test_chrisc_move` (in `host-gfx3d` / `host-gates`).
- **Residual risk:** Full end-to-end Mine (voxel collision, jump, ground,
  input focus, vertical camera / pitch convention) is **not** covered by this
  host test and needs the QEMU Mine smoke + input-focus work (see OPEN items).

### SYS-01 — `SYS_WRITE` one-byte stack overflow (P0)

- **Subsystem:** kernel syscalls (`kernel/metal/syscall.c`).
- **Symptom:** A user `write(1, buf, 80)` corrupts one byte past an 80-byte
  kernel stack buffer.
- **Root cause:** `uint8_t buf[80];` with guard `n > 80u` (accepts `n == 80`),
  then `buf[n] = 0` writes `buf[80]` — out of bounds.
- **Files:** `kernel/metal/syscall.c` (`SYS_WRITE`).
- **Fix:** Size the buffer `buf[81]` so the NUL at `buf[n]` (n ≤ 80) is in
  bounds; guard unchanged.
- **Verification:** By inspection + reasoning; kernel rebuilds clean under
  `-Wall -Wextra -Werror`. Automated regression requires a QEMU user-process
  write test (see QEMU-GATES OPEN) which is not yet in the gates.
- **Residual risk:** Broader `copy_from_user`/`copy_to_user` per-page
  validation and overflow audit remain OPEN (see USERCOPY-01).

---

## OPEN (confirmed in code, not yet fixed)

These were confirmed by code inspection during the audit and are documented so
they are not lost. They are **not** fixed in this pass. Ordered roughly by the
campaign phases.

| ID | Sev | Subsystem | Confirmed location | Summary |
| --- | --- | --- | --- | --- |
| KTHREAD-01 | P0 | kthread/SMP | `kernel/metal/kthread.c:20-23` (`g_cur`, `g_run`, `g_saved_rsp` globals) | Global current-thread / saved-RSP shared across CPUs; two CPUs in `kt_run()` overwrite each other's context. Needs per-CPU context. |
| JIT-01 | P0 | JIT context | `compiler/jit/jit.c:19-20` (`g_jit_vm`, `g_jit_user` globals) | JIT syscall trampoline context is global; one VM can clobber another's. Needs per-execution/CPU/VM context. |
| JIT-02 | P0 | JIT lifetime | `compiler/jit/jit_compile.c` (global `g_nat`/`g_psite`/`g_ptgt`), `jit_free` | Executable mappings must be unmapped + TLB-invalidated before physical pages return to the PMM; compilation state is global. |
| CLVM-ISO-01 | P0 | CLVM sync objects | `kernel/lang/clvm_sys.c:96,109` (`th_wake_mutex`/`th_wake_cond` keyed on guest addr only) | Two apps with the same guest offset share mutex/cond identity; must key by `(VM/slot, guest_address)`. |
| USERCOPY-01 | P0 | syscalls/user mem | `kernel/metal/syscall.c` `copy_from_user`/`copy_to_user` | Needs per-address-space page-presence validation and `uaddr + n` overflow checks. |
| ELF-01 | P0 | ELF loader | `kernel/metal/elf.c` (`g_elf_pid`, partial rollback) | Malformed/partial loads leave mappings/CR3; needs transactional cleanup + overflow-safe validation. |
| PMM-HEAP-01 | P0 | PMM/heap/SMP | `kernel/metal/pmm.c`, `heap.c` | Allocator concurrency correctness under multiple CPUs must be audited/locked; needs multicore stress test. |
| FD-OWN-01 | P1 | native/CLVM FDs | `g_ufile[]`, `g_fds[]` | FD tables lack per-process/VM ownership validation. |
| INPUT-FOCUS-01 | P1 | input | `input_key_down()` global `g_keys[]`; extended-scancode `E0` stripped | Background apps still see keys; extended keys collide with keypad; needs focus ownership + distinct extended representation. |
| GFX3D-CTX-01 | P1 | 3D graphics | `math3d`/`zbuf`/`shade`/`voxel` globals | Multiple 3D apps contaminate shared camera/zbuf/texture state; needs per-app `Gfx3DContext` or an enforced single-context invariant. |
| MINE-PITCH-01 | P1 | camera convention | `kernel/gfx/math3d.c` (`f.y = -sp`) vs `PLAY.CC` dy inversion | Pitch convention inconsistent across mouse/arrows/`cam()`/renderers; pick one and apply everywhere. |
| GATES-01 | P2 | build/test gates | `makefile` | Orphan `test*` targets exist outside gates; QEMU gates lack a 1/4-CPU matrix and treat `-timeout` non-zero exit as success. Needs a marker-scanning QEMU wrapper. |

See `docs/STABILITY_REPORT.md` for the campaign report and scope.
