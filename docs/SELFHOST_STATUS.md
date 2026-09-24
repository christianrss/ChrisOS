# Self-host status

Recorded at the start of the developer-toolkit campaign. A level is PROVEN
only when an automated gate shows it. A capability that merely exists in a
running system is not PROVEN.

Requested base: `dc662538477d7322312a2d54c983207b5b6259d7`.
Work base: `1e81ca46d41c136f2a842d0a43ee8d1fee0b4268` on
`cursor/foundation-campaign-7c6f` (foundation fixes kept).

| Level | Meaning | Status | Gate |
| --- | --- | --- | --- |
| SH0 | Host bootstrap. An external toolchain builds the kernel and the ChrisC bootstrap compiler. | PROVEN | `make host-gates` builds and runs the host test set, including the compiler. `make kernel` is the image build. Both already existed before this campaign. |
| SH1 | Applications are compiled by a compiler running as a program inside ChrisOS, and that result is executed there. | NOT PROVEN | `sys_cc` compiles inside the OS, but it calls `compiler/chrisc` linked into the kernel. That is still the bootstrap compiler. There is no QEMU gate with a marker that an in-OS compile produced a program and ran it. |
| SH2 | The in-OS compiler compiles a new copy of itself, and that copy compiles again, with equivalent behavior. | NOT PROVEN | No stage1/stage2 gate. `APPS/CC` is not that compiler. |
| SH3 | The userspace and toolkit rebuild inside the OS. | NOT PROVEN | No gate. |
| SH4 | The kernel rebuilds inside the OS from real kernel sources. | NOT PROVEN | KCC does not compile the kernel. ChrisBuild's file set is still a small demo. |
| SH5 | An internally produced `BIN/KERNEL.ELF` is installed and the machine boots that ELF. | NOT PROVEN | No gate. The installer gate boots a host-built kernel. |
| SH6 | Install media and the system image rebuild with no external toolchain. | NOT PROVEN | Limine, GCC, and the host build are still required. |

## What must not be counted

This is not SH2:

```
ChrisIDE -> SYS 100 -> compiler/chrisc.o inside the kernel
```

That path may keep working. Its name is bootstrap compile.

`APPS/CC/CC.CC` compiling `HELLO.CC` is not SH2 either. The conformance
matrix in `docs/CHRISC_SELFHOST_CONFORMANCE.md` shows the missing language.

## Gates added with this campaign

These gates are about the toolkit base, not about a self-host level:

- `test_editor_path`
- `test_cdbg`
- `test_dbg_step`
- `test_editmodel`

They are part of `make host-gates`. None of them flips SH1 or higher.

## Next gate that would move a level

SH1 moves only after a QEMU (or equivalent in-OS) run compiles a ChrisC
application with a compiler that is itself a ChrisC program, then runs the
result, and the log contains an explicit success marker. Using `sys_cc`
does not qualify.
