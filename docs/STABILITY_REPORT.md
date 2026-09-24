# ChrisOS stability report

The campaign is not finished. Items below are stable only where a test is
named. Everything else stays experimental.

## Heads

- Previously analyzed: `2550b9cab06c3ee2e81a292691031935ec5613df`
- `feat/os2` at the start of this branch: `92ec452178d7e3e7ae34546504a16e3f565e0c05`
  (phase 1 already present: ChrisC float pointers, SYS_WRITE buffer)
- Work branch: `cursor/stability-campaign-7c6f`

## P0 fixed in code, with a host test

- CHRISC-01/02 and CHRISC-03 (phase 1, already on `feat/os2`): `float *` is a
  pointer, `*p` of a float is a float, compound assignment keeps float-ness.
  Tests: `test_chrisc_ptr_float`, `test_chrisc_move`. `LIB/SIM.CC` unchanged.
- PMM-HEAP-01: PMM and heap locks. Test: `host-pmm-heap-smp-test` and the
  ASan/UBSan build in `host-sanitize`.
- KTHREAD-01: per-CPU thread context. Test: `host-kthread-smp-test`.
- ELF-01: validation and rollback. Test: `host-elf-malformed-test`.
- CLVM-ISO-01: mutex/cond identity is `(slot, address)`. Test:
  `host-clvm-sync-test`.
- USERCOPY-01: copy walks the process page tables. No dedicated host test.
- JIT-01/02: per-CPU trampoline, serialized compile, unmap before free.
  `host-jit-test` passed. No new differential stress.

## P1 fixed in code, with a host test where noted

- MINE-01 movement math via ChrisC (phase 1): `test_chrisc_move`.
- MINE-PITCH-01: positive pitch looks down, matching `math3d`. Captured mouse
  deltas, extended arrow codes. Test: `test_math3d_view`. `MINE.CLV` rebuilt.
  Not booted.
- INPUT-FOCUS-01: extended scancodes, capture, focus gate in `key()`. Test:
  `test_keystate`, `host-input-test`. The focus gate itself is not in that host
  binary.
- JOB-01: full queue returns 0. Test: `host-job-saturate-test`.
- NET-01: socket owner and CLVM slot. Test: `host-sock-owner-test`.
- FD-OWN-01: native owner and CLVM slot checks. No two-app FD test.
- AC97-01: DMA failure cleanup, IRQ-safe ring, event sequence. No host test.
- SYS-01: `buf[81]` from phase 1. No automated regression.

## Still open

See the OPEN table in `docs/STABILITY_AUDIT.md`. The largest gaps are
filesystem locking, 3D/voxel contexts, storage-driver timeouts, toolkit
teardown, `LIB/` audit, fuzz tests, and actually running QEMU.

## Gates run in this session

Passed:

- `host-pmm-heap-smp-test`
- `host-kthread-smp-test`
- `host-job-saturate-test`
- `host-clvm-sync-test`
- `host-elf-malformed-test`
- `host-sock-owner-test`
- `test_keystate`
- `test_math3d_view`
- `host-input-test`
- `host-jit-test`
- `host-sanitize` (ASan+UBSan on PMM/heap, kthread, job, keystate)
- `tools/check_test_gates.py` (reachable from `host-gates`)
- `mk_clv` rebuilt `GAMES/MINE/MINE.CLV` and `GAMES/WORLD.CLV`
- Freestanding compile of `mm.c`, `proc.c`, `elf.c`, `syscall.c`, `kthread.c`,
  `job.c`, `jit.c`, `jit_compile.c`, `ac97.c`, `clvm_sys.c`, `input.c`,
  `wm/main.c`

Not run:

- `host-gates` as a whole (ChrisC/Doom/CFS suite not repeated this session)
- any QEMU target (`qemu-system-x86_64` is absent, `third_party/limine` is absent)
- `make stability` / `make qemu-stress` (those targets are not fully defined;
  `host-stress` exists)

## SMP

Host tests use 4 pthreads and per-CPU signatures. No two of those threads
received the same physical page. This is not a boot of the kernel with 4
vCPUs. `QEMU_HEAD` now passes `-smp $(QEMU_SMP)` with default 4, and
`full-gates` also runs the ATA test at 1 CPU. That configuration was not
executed.

## Mine Chris

Language-level integration matches the specified `float *` case (phase 1).
Look uses the math3d convention: pointer down increases pitch (look down),
pointer up decreases it, world +Y stays above the horizon in
`test_math3d_view`. WASD key codes are unchanged. The image was recompiled.
Collision, gravity, jump, and ground were not executed in a world.

## Doom

Not re-run after this branch's ChrisC builtin additions. Phase 1 reported
`test_doom_compile` and `test_doom_engine` passing on `feat/os2`. That result
is not repeated here.

## Drivers

The QEMU makefile recreates ATA/AHCI/NVMe/VirtIO/USB images and routes them
through `tools/qemu_gate.py`. No driver was booted.

## ChrisC

Float-pointer behavior from phase 1 stands. This branch adds `mouse_dx`,
`mouse_dy`, `mouse_cap`, and `mouse_rel` builtins so games can read captured
deltas. `MINE.CC` compiled with those builtins.
