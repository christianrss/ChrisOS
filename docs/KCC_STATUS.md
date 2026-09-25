# KCC status

Profile: `docs/CHRIS_KERNEL_C_PROFILE.md`.
The audit of the tree at the start of the native-toolchain campaign is
`docs/NATIVE_TOOLCHAIN_AUDIT.md`. This page is the current host gate.

## What the host gate proves

`host-kcc-kernel-l0` runs `host-kcc-test`. `host-kcc-test` is a dependency
of `host-gates`.

The test compiles `tools/kcc_fixtures/level0.c` and requires `kstart`, an
undefined `outb`, and an `R_X86_64_PLT32` relocation.

It compiles `kernel/metal/serial.c` and requires `serial_init`,
`serial_putc`, `serial_puts`, `serial_write_hex`, `serial_write_u64`,
rodata, and BSS objects `serial_available` and `g_serial_lock`.

It compiles `kernel/metal/klog.c` and requires `klog_init`, `klog_putc`,
`klog_puts`, `klog_copy`, a BSS object named `g_log`, and a BSS section of
at least 8192 bytes.

It links those two objects with ChrisAsm stubs for `inb`, `outb`,
`spin_init`, `spin_lock`, and `spin_unlock`. The ELF has two program
headers and passes `chrisld_validate`.

It also compiles a small volatile fixture. Two stores to one plain
`uint32_t` global become the last store. Two stores and two loads of a
`volatile uint32_t` stay in the assembly, and the volatile accesses are
32-bit (`mov dword`). ChrisAsm accepts that `dword` form.

It compiles `kernel/metal/string.c` (`memset`, `memcpy`, `memcmp`,
`strlen`, `strncpy`, `strcmp`, `strncmp`), `kernel/metal/pit.c`, and
`kernel/metal/meminfo.c` (`mem_format`, seven parameters, the seventh on
the stack). A layout fixture checks that `Pair.b` is at offset 4 and a
packed `Tight.b` is at offset 1.

Passing this gate does not mark SH4.

## Still outside the gate

On this tree the host compiler accepts `ioapic.c`, `klog.c`, `meminfo.c`,
`pit.c`, `serial.c`, and `string.c`. The other `kernel/metal` files still
fail. The usual stop is GNU inline assembly (`port.c`, `panic.c`,
`spin.c`, `tlb_proto.c`), a missing `limine.h` (`smp.c`, `bootinfo.c`,
`start.c`), or `_Static_assert` / `sizeof`.

`kernel/metal/port.c` is GNU inline assembly and is not compiled. A global
array accepts a bound and a semicolon. An initializer on a global array is
rejected. ChrisAsm does not implement `cli`, `hlt`, or `invlpg`. ChrisLd
has not linked `BIN/KERNEL.ELF`. There is no GCC differential run.
