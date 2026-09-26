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
- SYS-01: `buf[81]` plus `syscall_write_term`. Test: `host-sys-write-test`.

## Fixed in this continuation, with a host test

- FS-LOCK-01: yielding CFS lock, kernel holder is the CPU id. Tests:
  `host-cfs-lock-test`, `host-cfs-test`.
- GFX3D-CTX-01: per-app camera, light, and texture. First load uses defaults,
  not the previous app. Voxel world has one owner. Test:
  `host-gfx3d-ctx-test`.
- SYS-01: `host-sys-write-test`.
- DOOM-RERUN-01: `test_doom_compile`, `test_doom_engine` (85 files). The
  missing-file diagnostic names the path (`host-chrisc-read-diag-test`).
- FUZZ-01 (partial): CFS, ELF, ChrisC, CLVM. BMP and packets are not fuzzed.
- TOOLKIT-01 (partial): 24 task open/close cycles in `host-task-window-test`.
  No heap/PMM comparison.
- DRV-TIMEOUT-01: block paths return `BD_ETIMEOUT` when the poll budget ends.
  QEMU covers the success path when those gates pass. No injected timeout.

## Still open

See the OPEN table in `docs/STABILITY_AUDIT.md`. Still without a runtime test:
AC97 waiter, hardware PMM leak loop, TLB poll timeout, `LIB/STRING.CC`
`memmove` narrowing, BMP/packet fuzz, Mine boot, and a full `static g_*`
inventory.

## Gates run in this session

Host, passed:

- `host-cfs-test` (`host cfs tests passed`)
- `host-cfs-lock-test`, `host-gfx3d-ctx-test`, `host-sys-write-test`
- `host-fuzz-cfs-test`, `host-fuzz-elf-test`, `host-fuzz-chrisc-test`,
  `host-fuzz-clvm-test`, `host-chrisc-read-diag-test`, `host-task-window-test`
- `test_doom_compile` (`code=15627`, plus `test_phys_compile`)
- `test_doom_engine` (`code=1209611`, 85 files)
- `tools/check_test_gates.py` (`ok (209 reachable)`)
- Freestanding compile of `cfs.c`, `clvm_sys.c`, `ac97.c`, `ahci.c`, `nvme.c`,
  `virtio_blk.c`, `usb_msc.c`, and a full kernel link to `build/os.iso`

Host, not run as one target:

- `host-gates` / `host-stress` / `host-sanitize` were not repeated as a single
  `make` after these targets were added. The new tests above were run on
  their own.

QEMU, passed (markers required by `tools/qemu_gate.py`):

- `test-qemu-ata` at 4 CPUs: `cpu_online_count=4`, `root ata`, `cfs mounted`,
  and the log also contains `desktop 60Hz`
- `test-qemu-smp1`: `cpu_online_count=1`, `root ata`, `cfs mounted`
- `test-qemu-ahci`, `test-qemu-nvme`, `test-qemu-vblk`, `test-qemu-usb`,
  `test-qemu-gpu`
- `test-qemu-noata`: `ata missing`, `root ahci`, `cfs mounted`,
  `install selftest ok`, `desktop 60Hz`

QEMU, not passed:

- `test-qemu-install`: serial reached `install backup gpt` and did not contain
  `install auto`, `install tree copied`, or `install gpt+esp+cfs disk=ahci`
  before the 300s gate ended. The installed-disk OVMF boot was not started.
- `test-qemu-riscv`: `qemu-system-riscv64` is installed.
  `riscv64-unknown-elf-gcc` is not, so the RISC-V kernel was not built.

## SMP

Host tests use 4 pthreads. The kernel was also booted under QEMU with
`-smp 4` and `-smp 1`. Both printed the matching `cpu_online_count`. There is
no separate 2-CPU rule.

## Mine Chris

Language-level integration matches the specified `float *` case (phase 1).
Look uses the math3d convention: pointer down increases pitch (look down),
pointer up decreases it, world +Y stays above the horizon in
`test_math3d_view`. WASD key codes are unchanged. The image was recompiled.
Mine was not launched from the desktop, so collision, gravity, jump, and
ground were not executed in a world.

## Doom

`test_doom_compile` and `test_doom_engine` passed after checking out the
pinned `third_party/doomgeneric_src` gitlink (`dcb7a8d`). The earlier failure
named `I_INPUT.CC` because the next list entry was missing and the diagnostic
still pointed at the previous file.

## Drivers

ATA, AHCI, NVMe, VirtIO Block, and USB MSC completed their QEMU read/write
markers. An empty ATA status no longer uses the full identify budget. VirtIO
waits up to 4096 yields for the status byte after the used index. No test
injects a device that never completes.

## ChrisC

Float-pointer behavior from phase 1 stands. This branch adds `mouse_dx`,
`mouse_dy`, `mouse_cap`, and `mouse_rel` builtins so games can read captured
deltas. `int` is 4 bytes and a pointer is 8; `LIB/STRING.CC` `memmove` still
casts pointers to `int`. `LIB/SIM.CC` was not modified.
