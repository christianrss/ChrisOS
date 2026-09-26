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

It also compiles every `kernel/metal/*.c` file and checks one exported
symbol in each, including `bootinfo.c`, `pmm.c`, `gdt.c`, `mm.c`,
`smp.c`, `spin.c`, `start.c` (`kstart`), and `user_enter.c`. `port.c`
must contain the bytes for `in al,dx`, `in ax,dx`, `in eax,dx`,
`out dx,al`, `out dx,ax`, `out dx,eax`, and `hlt`. `bootinfo.c` must
start its data section with the Limine requests marker
`ae d1 e7 9d b3 f4 b8 f6`. `gdt.c` must contain `lgdt [rax]` (`0f 01 10`),
`push 8` (`6a 08`), `lretq` (`48 cb`), `mov ax, 0x10` (`66 b8 10 00`),
and `str ax` (`66 0f 00 c8`). `spin.c` must contain
`lock cmpxchg dword [rcx], edx` (`f0 0f b1 11`). `user_enter.c` must
contain `iretq` (`48 cf`).

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
  barrier, the `port.c` `in`/`out` templates, `mov %%cr2`/`%%cr3`/`%%rsp`,
  `mov` into `%%cr3`, `invlpg (%0)`, `lidt %0`, and `str %0`. The fixed
  multi-instruction blocks in `gdt.c`, `smp.c`, `kthread.c`, and
  `user_enter.c` are encoded instruction by instruction. Any other
  template, including `iretq` alone, still fails.
- `__sync_bool_compare_and_swap`, `__sync_fetch_and_add`, and
  `__sync_lock_release` on `uint32_t *` or `uint64_t *` emit `lock cmpxchg`,
  `lock xadd`, and a zero store. Other `__sync_*` and `__atomic_*` builtins
  fail. `__builtin_return_address(0)` is `mov rax, [rbp+8]` because every
  function has that frame. Any other level fails.
- `float` is a 4-byte field type so `math3d.h` can be included. Loading or
  storing a float value fails. Float is not implemented as an integer.

`sizeof(int)` is 8 in this compiler. `uint32_t` is 4. `int` was already
8 bytes before `sizeof` existed. Do not treat `sizeof(int)` as 4.

Passing this gate does not mark SH4. Nothing here is booted.

## Still outside the gate

A host probe of the makefile's C list compiled 50 of 112 translation
units. The other 62 still fail. The repeated stops are a `union` in
`kernel/wm/task.h`, a numeric brace initializer (`sha256.c`, `font.c`),
`#include <string.h>`, a full 1536-byte frame, float arithmetic in the
GFX units, and `kernel/gfx/sse_init.c`. `kernel/metal/idt_stubs.asm` is
still NASM. Section attributes are skipped, so the Limine requests land
in `.data` and ChrisLd does not yet place them in a requests `PT_LOAD`.
ChrisLd has not linked `BIN/KERNEL.ELF`. There is no GCC differential
run and no QEMU boot of this object code. SH4 is not proven.
