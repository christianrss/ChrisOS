# Current self-host audit

Definitions match the campaign levels. A level is proven only by a gate
that shows it. This pass did not add that gate.

| Level | Class | Why |
| --- | --- | --- |
| SH0 | HOST-TESTED on earlier trees | Host GCC and `ld` build the image. `make host-gates` is the host suite. It was not re-run as a whole here. |
| SH1 | NOT PROVEN | In-kernel ChrisC is the bootstrap compiler. No QEMU marker shows a compiler that is itself a ChrisOS program compiling and running an application. |
| SH2 | NOT PROVEN | No stage-1 and stage-2 compiler gate. |
| SH3 | NOT PROVEN | Libraries, desktop, and tools are not rebuilt inside the OS by a gate. |
| SH4 | NOT PROVEN | `host-kcc-test` compiles the level-0 fixture, `serial.c`, `klog.c`, `string.c`, `pit.c`, and `meminfo.c`. It keeps volatile MMIO accesses and checks one packed struct offset. Six `kernel/metal` files compile. The rest do not. ChrisAsm does not implement `cli`, `hlt`, or `invlpg`. `chrisld_link_objects` has not linked `BIN/KERNEL.ELF`. |
| SH5 | NOT PROVEN | No install of an internally built kernel and no reboot that prints that kernel's hash. |
| SH6 | NOT PROVEN | A successor install still needs the host toolchain. Limine stays external, which the level allows, but the OS cannot yet build itself. |

Blocker for every level past SH0: the native toolchain does not compile
the kernel. Six `kernel/metal` files compile on the host. That does not
move SH4. The next real step is the files stopped by inline assembly,
`limine.h`, and `sizeof`, with a GCC differential test where the result
is observable.
