# ChrisOS Stability Audit

Living tracker for the stabilization campaign. Previously analyzed HEAD
`2550b9cab06c3ee2e81a292691031935ec5613df`. Confirmed `feat/os2` HEAD before
this branch: `92ec452178d7e3e7ae34546504a16e3f565e0c05` (phase-1 squash of the
ChrisC float-pointer fixes and SYS_WRITE, one commit after `2550b9c`).
Work continues on `cursor/stability-campaign-7c6f`.

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
- **Fix:** Size the buffer `buf[81]`. `syscall_write_term` rejects `n > 80` or a
  capacity shorter than `n+1` before it writes the NUL.
- **Regression test:** `host-sys-write-test` (ASan/UBSan).
- **Residual risk:** The host test calls the helper, not a user process in QEMU.

---

### PMM-HEAP-01 — physical and heap allocators unsynchronized (P0)

- **Subsystem:** PMM, heap, SMP.
- **Symptom:** Two CPUs could observe the same free bit and receive the same physical page, or split the same heap block.
- **Root cause:** `pmm_alloc` / `kmalloc` mutated the bitmap, cursor, and arena list with no lock. `pmm_foreach_free_run` callbacks re-enter the allocator.
- **Fix:** IRQ-safe recursive PMM spinlock (same CPU may re-enter). Heap lock is non-recursive and is taken before PMM when the heap grows. Lock order is heap → PMM; PMM never takes the heap lock.
- **Regression test:** `host-pmm-heap-smp-test` (4 threads, per-CPU signatures, duplicate check, contiguous alloc, cross-CPU free, ASan/UBSan via `host-sanitize`).
- **Residual risk:** Host stress is not the kernel's interrupt path. A double-free still panics by design and is not an automated "detect and return" test.

### KTHREAD-01 — one global saved RSP for every CPU (P0)

- **Subsystem:** kthread.
- **Symptom:** Two CPUs inside `kt_run` could restore each other's stack.
- **Root cause:** `g_cur`, `g_run`, and `g_saved_rsp` were process-wide globals.
- **Fix:** Current/run pointers are per CPU (`g_cur_cpu`, `g_run_cpu`). Saved RSP lives on the thread. Slot allocation takes `g_slot_lock`. `kmalloc` failure returns -1 and does not run on the caller stack. Join drains the job queue only when no AP is online; with APs it waits.
- **Regression test:** `host-kthread-smp-test` (4 worker CPUs, 24 threads, stack canary, recursion, TLS).
- **Residual risk:** Host workers are pthreads calling `job_worker_once`. Kernel AP startup is not executed here (no QEMU).

### JOB-01 — a full queue panicked the SMP selftest (P1)

- **Subsystem:** job queue.
- **Root cause:** `smp_job_selftest` treated one failed `job_submit` as fatal even when the queue was only temporarily full.
- **Fix:** The selftest retries with `job_worker_once`. A full queue still returns 0 to the caller.
- **Regression test:** `host-job-saturate-test`.

### MM-01 — no unmap, user page tables leaked, TLB was local-only (P0)

- **Subsystem:** virtual memory.
- **Root cause:** Kernel mappings are shared (high half copied into every PML4). `invlpg` on the writer does not refresh other CPUs. Process teardown freed neither intermediate user tables nor the PML4. ELF/user frames were not unmapped before `pmm_free`.
- **Fix:** `unmap_4k`, `mm_unmap_cr3`, `mm_translate`, `mm_free_user_space`. `mm_tlb_shootdown` waits until every online CPU acks via `mm_tlb_poll` (workers and the desktop idle loop). User processes stay BSP-only: `proc_switch` panics off the BSP.
- **Regression test:** freestanding compile of `mm.c` / `proc.c`. No QEMU shootdown run in this environment.
- **Residual risk:** A CPU that stops polling (long job without `mm_tlb_poll`) trips `tlb shootdown timeout`. JIT compile scratch stays global and is serialized by `g_jit_compile_lock` instead of a per-compile context.

### ELF-01 — loader left a live process and could map into the kernel (P0)

- **Subsystem:** ELF loader.
- **Root cause:** `g_elf_pid` plus `elf_zero_user` falling back to `map_4k` on the kernel CR3. Errors returned without `proc_destroy` or restoring the previous process. `filesz`/`vaddr`/`phoff` used wrapping additions. Pages were not recorded, so destroy could not free them.
- **Fix:** Validate every segment before `proc_create` (overflow-safe spans, `filesz <= memsz`, non-zero `memsz`, W^X rejected, overlap, entry inside an executable segment, load window). Map with `proc_map_owned`. On failure, destroy the process and switch back. `mm_map_cr3` returns an error if a page-table page cannot be allocated.
- **Regression test:** `host-elf-malformed-test` (truncated header, bad magic, phoff/vaddr/offset overflow, filesz>memsz, zero memsz, outside window, overlap, unsupported PH, W+X, entry not executable, mid-load OOM accounting, one successful load).
- **Residual risk:** The host stub does not model real page tables. More than 32 program headers is rejected.

### USERCOPY-01 — user copy trusted a virtual window (P0)

- **Subsystem:** syscalls.
- **Fix:** `copy_from_user` / `copy_to_user` reject a wrapping length, reject addresses at or above the lower non-canonical half, and copy through `mm_translate` of the current process CR3 plus the HHDM. Writes require a user-writable leaf. Native `g_ufile` entries store `owner` and are closed from `proc_destroy`.
- **Residual risk:** No dedicated host test. `SYS_WRITE` overflow (SYS-01) is still inspection-only. Syscalls from an AP return an error (BSP-only invariant).

### FD-OWN-01 — file descriptors were a global table (P1)

- **Subsystem:** native FDs and CLVM FDs.
- **Fix:** Native ops check `owner == proc_current()`. CLVM read/write/close check `g_fds[fd].slot` against the calling VM. `fd_free` returns -1 when a dirty flush does not write the full size, and `fclose` pushes that result.
- **Residual risk:** No two-app host test of the CLVM FD table (the check is in `clvm_sys.c`). Native FD ownership has no host harness.

### CLVM-ISO-01 — mutex identity was a guest address (P0)

- **Subsystem:** CLVM threads.
- **Fix:** `clvm_sync_same(slot, addr)` in `kernel/lang/clvm_sync.h`. Wake helpers pass the calling slot.
- **Regression test:** `host-clvm-sync-test`.

### JIT-01 / JIT-02 — trampoline context and executable lifetime (P0)

- **Subsystem:** JIT.
- **Fix:** Syscall trampoline context is `g_jit_ctx[smp_current_cpu()]`. `jit_compile_image` holds `g_jit_compile_lock` around the global scratch buffers. `jit_free` unmaps, shootdowns, returns the VA to a freelist, then `pmm_free_contig`. `jit_seal` maps without per-page shootdown and shootdowns once.
- **Residual risk:** Compile scratch is still global (serialized, not per-compile). The VA freelist reuses only equal sizes. No new interpreter-vs-JIT differential stress beyond the existing host JIT tests. `host-jit-test` passed after the link change; `host-jit-vm-test` was not re-run in this session.

### INPUT-FOCUS-01 — global keys and E0 stripped (P1)

- **Subsystem:** input, CLVM `key()`.
- **Root cause:** Make codes were stored after dropping the E0 prefix, so arrows collided with the keypad. `key()` read `g_keys` with no focus check. Absolute tablet position was the only pointer stream.
- **Fix:** Extended makes use index `128+code` (`INPUT_SCAN_UP/LEFT/RIGHT/DOWN`). `key()`, `mouse_x/y`, and buttons return empty when the slot's task exists and is not focused. Capture is per task; repeating `mouse_cap` for the same owner does not clear deltas. ESC releases capture. Closing a slot releases it. Deltas come from PS/2 packets or from successive absolute positions only while a capture is held. `mouse_dx`/`mouse_dy` share one snapshot.
- **Regression test:** `test_keystate` (keypad vs arrows, shift/ctrl/alt/AltGr, layout, PS/2 sign, ESC, absolute capture). `host-input-test` still passes.
- **Residual risk:** Focus gating lives in the CLVM syscall path and is not executed by the host key test. Autorepeat is covered as a second make leaving the key down.

### MINE-PITCH-01 — look sign fought math3d (P1)

- **Subsystem:** Mine camera.
- **Convention:** positive pitch looks down (`math3d` `f.y = -sp`). Screen Y grows downward, so a positive `mouse_dy` increases pitch. Up arrow decreases pitch.
- **Fix:** `PLAY.CC` uses captured `mouse_dx`/`mouse_dy` and extended arrow codes. `WORLD.CC` arrows use the same codes. `MINE.CLV` and `WORLD.CLV` were rebuilt. `LIB/SIM.CC` was not modified.
- **Regression test:** `test_math3d_view` (pitch +30 looks down, pitch -30 looks up, world +Y projects above the horizon).
- **Residual risk:** No QEMU frame of Mine. Collision, gravity, jump, and ground are still only the language-level `test_chrisc_move` path.

### NET-01 — any caller could use any socket (P1)

- **Subsystem:** network.
- **Root cause:** `Sock.owner` was stored and then ignored. `sock_recv` rewrote it to the current process, so a recv stole the socket.
- **Fix:** `slot` identifies a CLVM app (`-1` means native). Native ops require `owner == proc_current()`. CLVM syscalls pass `vm_sync_slot`. Accepted connections copy both. `sock_close_slot` runs when an app slot closes. `sock_close_proc` runs from `proc_destroy`. The recv path no longer rebinds ownership.
- **Regression test:** `host-sock-owner-test`.
- **Residual risk:** No packet-level host test. IRQ receive still keys by the TCP tuple, which is required for demux.

### AC97-01 — DMA leak and unlocked ring (P1)

- **Subsystem:** AC97.
- **Fix:** If the second DMA page fails, the first is freed. The ring and event sequence use an IRQ-safe spinlock; the event flag is a sequence counter so a second IRQ is not lost behind a boolean.
- **Residual risk:** The waiter is one pid (`g_ac97_waiter`), published with an atomic store and read by the ISR. No host test (port I/O). See AC97-WAKE-01.

### GATES-01 — QEMU failures were ignored (P2)

- **Subsystem:** build.
- **Fix:** `tools/qemu_gate.py` requires every `--expect` marker and rejects panic, unexpected exception, double fault, general protection, heap corruption, and PMM corruption. Exit 124 (timeout while the kernel keeps running) is a pass only when those checks hold. `QEMU_SMP` defaults to 4. `full-gates` also runs ATA at 1 CPU. `test-qemu-install` is in `qemu-gates`. Disk images are recreated each run. `tools/check_test_gates.py` fails `host-gates` if a `test_*` or `host-*-test` target is unreachable. `host-stress` and `host-sanitize` exist.
- **Residual risk:** See QEMU-RUN-01 for which boots were actually executed. A 2-CPU target is not a separate rule; the makefile runs 1 and 4.

---

### FS-LOCK-01 — CFS metadata had no owner (P1)

- **Subsystem:** CFS.
- **Root cause:** Public CFS operations ran with no lock. A spinlock held across disk polling would also stall IRQs, so the lock yields with interrupts enabled.
- **Fix:** `g_cfs_lock` in `kernel/fs/fs_lock.h`. The kernel holder is `smp_current_cpu()+1` (a weak host default of 1 let every CPU look like the owner). Same holder re-enters.
- **Regression test:** `host-cfs-lock-test` (two threads, full reads are all A or all B, then fsck). `host-cfs-test` still passes.
- **Residual risk:** Syscall CFS stays on the BSP. An AP that calls CFS is excluded by the lock, but that path has no QEMU stress. Waiters pause; they do not drain the job queue.

### GFX3D-CTX-01 — 3D globals leaked across apps (P1)

- **Subsystem:** math3d / shade / tex / voxel.
- **Root cause:** Camera, light, and texture slot were process-wide. A first load that snapshotted the live globals copied the previous app into a context that had never run.
- **Fix:** `Gfx3DCtx` on `ClvmGfxCtx`. Dispatch loads it and saves it on every return. The first load installs the default camera `(0, 1.5, 5)`, the default light, and texture slot 1, and keeps the current viewport size. `voxel_claim` allows one slot; close releases it. The z-buffer storage was already per gfx slot.
- **Regression test:** `host-gfx3d-ctx-test`.
- **Residual risk:** One voxel world for the machine. Two apps cannot both own it.

### SYS-01-TEST — SYS_WRITE terminator (P2)

- **Subsystem:** syscalls.
- **Fix:** `syscall_write_term` rejects `n > 80` or a buffer shorter than `n+1` before writing the NUL.
- **Regression test:** `host-sys-write-test` under ASan/UBSan.

### DOOM-RERUN-01 — engine list could not be read (P2)

- **Subsystem:** Doom host suite / ChrisC diagnostics.
- **Symptom:** `test_doom_engine` reported `GAMES/DOOM/I_INPUT.CC:1:1 cannot read source file` even though that file exists.
- **Root cause:** `third_party/doomgeneric_src` is a gitlink and was not checked out, so the next list entry was missing. `fail()` named the previous translation unit because the read happened before that file was entered in the line map.
- **Fix:** A missing read now records the path that failed. The pinned doomgeneric tree (`dcb7a8d`) is what `ENGINE.LST` compiles.
- **Regression test:** `host-chrisc-read-diag-test`. `test_doom_compile` and `test_doom_engine` (85 files, `code=1209611`).

### FUZZ-01 — parsers had no garbage input (P2, partial)

- **Tests:** `host-fuzz-cfs-test` (random paths, then fsck), `host-fuzz-elf-test` (random bytes, page budget unchanged on failure), `host-fuzz-chrisc-test` (random source must return 0 or 1, then a valid `main`), `host-fuzz-clvm-test` (random images stay inside `ClvmLoadError`, a written image still parses).
- **Residual risk:** No fuzzer for BMP (`LIB/BMP.H` is ChrisC) or for network packets.

### TOOLKIT-01 — task slots after repeated close (P1, partial)

- **Test:** `host-task-window-test` opens and closes 24 `TASK_APP` windows and requires the slot to be empty.
- **Residual risk:** The host task test does not compare heap or PMM counts. Editor, Explorer, Shell, and Mine are not in that loop.

### DRV-TIMEOUT-01 — storage polls (P1, code + QEMU success path)

- **Fix:** AHCI `issue`, NVMe `wait_cq`, and VirtIO `kick` return `-2` when the spin budget ends. The block helpers map `-2` to `BD_ETIMEOUT`. ATA already returned `BD_ETIMEOUT`. USB MSC `td_wait` returns `-2` on its frame/guard limit and `usb_rw` maps it. Device errors stay `BD_EIO`. VirtIO's yield loop is 4096 `serial_putc` calls: 256 returned while QEMU had posted the used index and status was still `0xFF`.
- **Regression:** QEMU `bdev rw ok` markers for the drivers that the gates boot. There is no injected timeout fault.

## OPEN

| ID | Sev | Subsystem | Summary |
| --- | --- | --- | --- |
| AC97-WAKE-01 | P2 | AC97 | The ISR unblocks only `g_ac97_waiter`. No host test; port I/O is not stubbed. |
| PROC-LEAK-01 | P1 | processes | `host-elf-malformed-test` checks the host page stub. There is no create/run/destroy PMM counter loop on hardware. |
| TLB-POLL-01 | P2 | MM | Shootdown panics if an online CPU stops calling `mm_tlb_poll`. |
| LIB-AUDIT-01 | P2 | `LIB/` | `int` is 4 bytes and pointers are 8. `LIB/STRING.CC` `memmove` casts both pointers to `int`, and that cast narrows (`gen_narrow`). Guest heap offsets used today sit below 2GB, so the overlap direction matches. A guest address at or above 2^31 is still wrong. STDIO/MATH were not re-audited line by line. `LIB/SIM.CC` was not modified. |
| QEMU-RUN-01 | P1 | gates | Passed: ATA at 4 CPUs and at 1 CPU, AHCI, NVMe, VirtIO Block, USB MSC, virtio-gpu, and the no-ATA boot (`install selftest ok`, `desktop 60Hz`). `test-qemu-install` printed `install backup gpt` and did not reach `install auto` within 300s. RISC-V QEMU is installed; `riscv64-unknown-elf-gcc` is not, so `test-qemu-riscv` was not built. |
| GLOB-AUDIT-01 | P3 | kernel globals | The table below is the set this campaign touched. A full `static g_*` inventory is not finished. |

## Global state touched in this pass

| Symbol | Class |
| --- | --- |
| PMM bitmap, `pmm_cursor`, used/free counts | globally shared, PMM lock |
| heap arenas | globally shared, heap lock then PMM lock |
| `g_cur_cpu`, `g_run_cpu`, `KT.saved_rsp` | CPU-local / thread-local |
| `g_th[]` | globally shared, `g_slot_lock` |
| job queue | globally shared, queue spinlock |
| kernel PML4 high half, JIT VA window | globally shared; unmap + TLB shootdown |
| user PML4 low half, `g_current` | process-local; BSP-only invariant |
| `g_ufile[].owner` | process-local |
| `g_fds[].slot` | VM-local |
| CLVM mutex/cond wait | VM-local `(slot, guest address)` |
| `g_jit_ctx[]` | CPU-local |
| `g_nat` / patch arrays | globally shared, JIT compile lock |
| `g_keys[256]`, capture task, mouse deltas | device-local; CLVM reads gated by focus |
| `Sock.owner` + `Sock.slot` | process-local or VM-local |
| AC97 ring | device-local, IRQ-safe spinlock |
| `g_ac97_waiter` | one pid, atomic publish, ISR reads it |
| `g_cfs_lock` | globally shared; holder is the CPU id on the kernel |
| `ClvmGfxCtx.view3d` | per app slot (camera, light, texture) |
| z-buffer bytes | per gfx slot; global pointer is the binding |
| `g_voxel_owner` | one slot, or -1 when unclaimed |

See `docs/LOCKING.md`, `docs/RESOURCE_OWNERSHIP.md`, and `docs/STABILITY_REPORT.md`.
