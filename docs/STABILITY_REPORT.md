# ChrisOS Stability Campaign — Report

## Scope of this pass

This report covers the first executed slice of the stabilization campaign,
following the mandated methodology (reproduce → root-cause → failing test →
minimal correct fix → re-test → run related gates → check regressions →
document). It does **not** claim the whole 38-section campaign is complete;
remaining work is listed explicitly below and tracked in
`docs/STABILITY_AUDIT.md`.

- **Initial HEAD (`feat/os2`):** `2550b9cab06c3ee2e81a292691031935ec5613df`
  (confirmed as the current HEAD; no later commits existed to consider).
- **Work branch:** `cursor/stability-campaign-78d0` (off `feat/os2`).

## Completed (with regression tests)

### FASE 1 — ChrisC type system / float pointers / Mine movement

Root cause of the Mine movement failure was in the **language**, not the app,
so `LIB/SIM.CC` was **not** modified. Three coupled ChrisC defects:

- **CHRISC-01 (P0):** `float *` parameters were flagged as by-value floats
  (`arg_float = dt.isf`), truncating pointer arguments through the
  `FSTORE`/`FLOAD` path. Fixed to `dt.isf && !ptr`.
- **CHRISC-02 (P0):** `*p` on a float pointer was not typed float, so mixed
  arithmetic inserted a spurious `ITOF` that reinterpreted float bits as int.
  `N_DEREF.is_float` now derives from the pointee (new `deref_pointee_float`).
- **CHRISC-03 (P1):** compound assignment (`*p += x`, `f += x`) built its
  binary node without `is_float`, emitting integer ops on floats. Now
  propagated via `value_is_float` (keeps pointer arithmetic integer).

Verified by the campaign's exact minimal reproducer and surrounding cases.

### FASE 4 (partial) — syscall stack overflow

- **SYS-01 (P0):** `SYS_WRITE` wrote `buf[80]` on an 80-byte buffer when
  `n == 80`. Fixed by sizing `buf[81]`; guard unchanged.

## Tests added (wired into `host-gfx3d` → `host-gates`)

- `tools/test_chrisc_ptr_float.c` (`test_chrisc_ptr_float`): float*/int*
  parameters, deref, deref-assign, compound assign, multiple float* args.
- `tools/test_chrisc_move.c` (`test_chrisc_move`): `sim_move`-style velocity
  integration + `0.72` damping through `float*` out-params; asserts finite,
  coherent forward motion, damping on release, no cross-axis contamination,
  no NaN/Inf.

## Gates executed / results

- New tests: `test_chrisc_ptr_float: ok`, `test_chrisc_move: ok`.
- ChrisC/CLVM/DOOM correctness suite (no regressions): `test_chrisc_arrays`,
  `test_chrisc_float`, `test_chrisc_fn`, `test_chrisc_struct`,
  `test_chrisc_trig`, `test_chrisc_games`, `test_chrisc_include`,
  `test_chrisc_string`, `test_chrisc_c17`, `test_chrisc_lang`,
  `test_chrisc_apps`, `test_chrisc_doom`, `test_doom_compile`,
  `test_doom_engine`, `host-jit-vm-test`, `host-jit-native-test` — all `ok`.
- Kernel builds clean under `-Wall -Wextra -Werror` (`make iso`, exit 0),
  including `compiler/chrisc/chrisc.c` at `-O2`.

Toolchain note: the project builds cleanly with **gcc-11**; gcc-12+ emit a
false-positive `-Werror=array-bounds` in `chrisc.c` at `-O2`.

## ChrisC compatibility status

Pointer-to-float semantics now correct for: `float*`/`int*` parameters, `*p`
read, `*p = v`, `*p += v`/`*p *= v`, multiple `float*` args, `&arr[i].field`,
arrays of structs. Existing ChrisC/DOOM gates continue to pass.

## Mine Chris status

Physics/movement math (velocity integration + damping) is correct at the
language/runtime level and covered by `test_chrisc_move`. **Not yet verified
end-to-end:** voxel collision, gravity/ground/jump against a live world, input
focus/mouse capture, and the vertical-camera/pitch convention (MINE-PITCH-01).
These require the input-focus work and a QEMU Mine smoke test.

## Doom status

`test_doom_compile` and `test_doom_engine` pass (compile of the full engine
source). No Doom-specific hacks were added. Deeper runtime Doom validation is
part of the pending QEMU integration matrix.

## Not done in this pass (explicit)

The following campaign areas were **audited** (confirmed in code where noted in
`docs/STABILITY_AUDIT.md`) but **not fixed** here, and must not be considered
stable: SMP-safety of PMM/heap (PMM-HEAP-01), kthread per-CPU context
(KTHREAD-01), MM unmap/TLB shootdown, process teardown leak accounting, ELF
loader transactional cleanup (ELF-01), full `copy_from_user`/`copy_to_user`
validation (USERCOPY-01), native/CLVM FD ownership (FD-OWN-01), CLVM
mutex/cond isolation by `(VM, addr)` (CLVM-ISO-01), JIT per-context state and
executable-mapping lifetime (JIT-01/02), filesystem/CFS concurrency, storage
driver matrix (ATA/AHCI/NVMe/VirtIO/USB), AC97 IRQ sync, network socket
ownership, 3D render contexts (GFX3D-CTX-01), toolkit/app teardown stress,
`LIB/` audits, the QEMU 1/4-CPU gate matrix and non-ignored QEMU failure
detection (GATES-01), host sanitizer gates, and fuzz/property tests.

## Remaining limitations / still experimental

- ChrisC still overloads a single `is_float` flag to mean "pointee is float"
  on pointer symbols; a small internal type descriptor would make the compiler
  provably coherent (CHRISC-TYPE-REFACTOR, P3).
- SYS-01 has no automated regression yet (needs a QEMU user-process test).
- Multicore correctness is unverified; gates currently exercise limited CPU
  counts.
