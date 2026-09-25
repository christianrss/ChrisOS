# Current self-host audit

Definitions match the campaign levels. A level is proven only by a gate
that shows it. This pass did not add that gate.

| Level | Class | Why |
| --- | --- | --- |
| SH0 | HOST-TESTED on earlier trees | Host GCC and `ld` build the image. `make host-gates` is the host suite. It was not re-run as a whole here. |
| SH1 | NOT PROVEN | In-kernel ChrisC is the bootstrap compiler. No QEMU marker shows a compiler that is itself a ChrisOS program compiling and running an application. |
| SH2 | NOT PROVEN | No stage-1 and stage-2 compiler gate. |
| SH3 | NOT PROVEN | Libraries, desktop, and tools are not rebuilt inside the OS by a gate. |
| SH4 | NOT PROVEN | `host-kcc-test` compiles `tools/kcc_fixtures/level0.c`, `kernel/metal/serial.c`, and `kernel/metal/klog.c` (including the 8192-byte BSS ring) and links those two objects with port and spin stubs. It does not compile the kernel. `volatile` is discarded. ChrisAsm does not implement `cli`, `hlt`, or `invlpg`. `chrisld_link_objects` has not linked `BIN/KERNEL.ELF`. |
| SH5 | NOT PROVEN | No install of an internally built kernel and no reboot that prints that kernel's hash. |
| SH6 | NOT PROVEN | A successor install still needs the host toolchain. Limine stays external, which the level allows, but the OS cannot yet build itself. |

Blocker for every level past SH0: the native toolchain does not compile
the kernel. `serial.c` and `klog.c` are two host-compiled translation
units, linked with stubs. That does not move SH4. The next real step is
a volatile MMIO load and store that survive, then the rest of the kernel
files, with a GCC differential test where the result is observable.
