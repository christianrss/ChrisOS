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

It also compiles these real files and checks one exported symbol in each:
`acpi.c`, `apic.c`, `elf.c`, `heap.c`, `ioapic.c`, `job.c`, `pci.c`,
`port.c`, and `tlb_proto.c`. `port.c` must contain the bytes for
`in al,dx`, `in ax,dx`, `in eax,dx`, `out dx,al`, `out dx,ax`,
`out dx,eax`, and `hlt`.

A subset fixture checks four things that kernel C actually uses:

- `continue` in a `for` jumps to the step, and the step adds before
  jumping back to the condition. `break` jumps to the end. `break`
  outside a loop fails.
- `i == 1` and `bus << 16` reload the literal. The literal is not lost
  when the other operand is loaded.
- `~v` assembles as `not rax` (`48 f7 d0`). `sizeof(uint32_t)` is 4.
  `_Static_assert(sizeof(uint32_t) == 4)` passes. A false assert fails.
  An assert whose expression is not an integer constant fails.
- `__asm__ volatile` accepts `cli`, `sti`, `hlt`, `pause`, an empty
  barrier, and the `port.c` `inb`/`inw`/`inl`/`outb`/`outw`/`outl`
  templates. `mov %%cr3` and `__sync_fetch_and_add` fail.

`sizeof(int)` is 8 in this compiler. `uint32_t` is 4. `int` was already
8 bytes before `sizeof` existed. Do not treat `sizeof(int)` as 4.

Passing this gate does not mark SH4. Nothing here is booted.

## Still outside the gate

These `kernel/metal` files still fail on the host: `bootinfo.c`,
`buildid.c`, `gdt.c`, `idt.c`, `irq.c`, `kcc_job.c`, `kthread.c`, `mm.c`,
`panic.c`, `pmm.c`, `proc.c`, `ps2.c`, `smp.c`, `spin.c`, `start.c`,
`syscall.c`, and `user_enter.c`.

The stops that remain are `limine.h` (the include path and `#if`), an
empty `extern` array, a global brace initializer, `extern void (*name[N])(void)`,
`__sync_*` / `__builtin_*`, and inline asm this subset does not accept
(`mov %%cr3`, `mov %%cr2`, `mov %%rsp`, `invlpg`, `lidt`, `iretq`, and
multi-instruction templates). A global array still accepts a bound and a
semicolon. An initializer on a global array is rejected. ChrisLd has not
linked `BIN/KERNEL.ELF`. There is no GCC differential run.
