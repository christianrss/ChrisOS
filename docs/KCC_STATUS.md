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

Passing this gate does not mark SH4.

## Still outside the gate

`kernel/metal/port.c` is GNU inline assembly and is not compiled. A global
array accepts a bound and a semicolon. An initializer on a global array is
rejected. ChrisAsm does not implement `cli`, `hlt`, or `invlpg`. ChrisLd
has not linked `BIN/KERNEL.ELF`. There is no GCC differential run.
