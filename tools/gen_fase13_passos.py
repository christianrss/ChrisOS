#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "learn" / "fase13-usermode64"
OUT.mkdir(parents=True, exist_ok=True)


def fence_c(name: str, path: Path) -> str:
    text = path.read_text(encoding="utf-8")
    return f"### `{name}`\n\n```c\n{text}```\n\n"


def fence_asm(name: str, path: Path) -> str:
    text = path.read_text(encoding="utf-8")
    return f"### `{name}`\n\n```asm\n{text}```\n\n"


def fence_h(name: str, path: Path) -> str:
    text = path.read_text(encoding="utf-8")
    return f"### `{name}`\n\n```c\n{text}```\n\n"


p01 = """# PASSO 01 — GDT user e TSS rsp0

Tempo: 50 min. Anterior: [índice](INDEX.md) · Próximo: [PASSO_02.md](PASSO_02.md)

## Objetivo

Confirmar descritores user com `DPL=3` e `tss.rsp0` apontando para `__stack_top`.

O `kernel/metal/gdt.c` já deve ter:

- `gdt[3]` data user (`0xF2`)
- `gdt[4]` code user (`0xFA`)
- `gdt[5..6]` TSS com `rsp0 = __stack_top`

### `kernel/metal/gdt.h`

```c
#define GDT_KERNEL_CODE 0x08u
#define GDT_KERNEL_DATA 0x10u
#define GDT_USER_DATA   0x18u
#define GDT_USER_CODE   0x20u
#define GDT_TSS         0x28u
```

## Gate

`make` sem warnings. `gdt_read_tr() == 0x28` após `gdt_init()`.

Tag: `user64-01`.
"""

p02 = (
    """# PASSO 02 — iretq + syscall gate 0x80

Tempo: 70 min. Anterior: [PASSO_01.md](PASSO_01.md) · Próximo: [PASSO_03.md](PASSO_03.md)

## Objetivo

Entrar em ring-3 com `enter_user`, vector `0x80` com `DPL=3`, dispatch em C.

## Makefile

Adicione em `C_OBJECTS`:

```make
kernel/metal/syscall.o kernel/metal/user_enter.o \\
```

## Arquivos novos

"""
    + fence_h("kernel/metal/user_enter.h", ROOT / "kernel/metal/user_enter.h")
    + fence_c("kernel/metal/user_enter.c", ROOT / "kernel/metal/user_enter.c")
    + fence_h("kernel/metal/syscall.h", ROOT / "kernel/metal/syscall.h")
    + fence_c("kernel/metal/syscall.c", ROOT / "kernel/metal/syscall.c")
    + """### `kernel/metal/idt.h` (adicione)

```c
void idt_set_user_gate(unsigned int vector);
```

### `kernel/metal/idt.c` (trecho)

```c
void idt_set_user_gate(unsigned int vector) {
    idt_set_gate_attr(vector, isr_stub_table[vector], 0xee);
}
```

### `kernel/metal/irq.c` (trecho em `irq_dispatch`)

```c
if (frame->vector == 0x80u) {
    syscall_dispatch(frame);
    return;
}
```

### `kernel/metal/start.c`

```c
#include "syscall.h"
...
idt_init();
syscall_init();
```

`syscall_init()` chama `idt_set_user_gate(0x80)`.

Tag: `user64-02`.
"""
)

p03 = (
    """# PASSO 03 — loader ELF64 estático

Tempo: 70 min. Anterior: [PASSO_02.md](PASSO_02.md) · Próximo: [PASSO_04.md](PASSO_04.md)

## Objetivo

`elf_load` lê header/phdr little-endian, mapeia `PT_LOAD` com `map_4k(..., MM_USER|...)`.

## Makefile

```make
kernel/metal/elf.o \\
```

## Arquivos

"""
    + fence_h("kernel/metal/elf.h", ROOT / "kernel/metal/elf.h")
    + fence_c("kernel/metal/elf.c", ROOT / "kernel/metal/elf.c")
    + """Restrições do livro: `vaddr` em `0x400000..0x401000`, sem `INTERP`/`DYNAMIC`.

Tag: `user64-03`.
"""
)

p04 = (
    """# PASSO 04 — syscalls 1–3

Tempo: 50 min. Anterior: [PASSO_03.md](PASSO_03.md) · Próximo: [PASSO_05.md](PASSO_05.md)

## Tabela fechada

| rax | nome | args |
|----:|------|------|
| 1 | `sys_exit` | rdi=code |
| 2 | `sys_write` | rdi=1, rsi=buf user, rdx=len |
| 3 | `sys_putpixel` | rdi=x, rsi=y, rdx=color |

Implementação completa:

"""
    + fence_c("kernel/metal/syscall.c", ROOT / "kernel/metal/syscall.c")
    + """User chama `int 0x80`. `sys_exit` reescreve `rip/cs` do frame para voltar ao caller de `enter_user` (usa `TSS.rsp0`).

Tag: `user64-04`.
"""
)

p05 = (
    """# PASSO 05 — BIN/HELLO.ELF + shell `runelf`

Tempo: 50 min. Anterior: [PASSO_04.md](PASSO_04.md) · Próximo: [PASSO_06.md](PASSO_06.md)

## User flat ELF

"""
    + fence_asm("user/hello.asm", ROOT / "user/hello.asm")
    + fence_c("tools/cfs_put_file.c", ROOT / "tools/cfs_put_file.c")
    + """## Makefile

```make
user/hello.elf: user/hello.asm
	nasm -f bin user/hello.asm -o user/hello.elf

host-cfs-put-file: tools/cfs_put_file.c kernel/fs/cfs.c
	$(HOST_CC) $(HOST_CFLAGS) -Ikernel/fs \\
		-o tools/cfs_put_file tools/cfs_put_file.c kernel/fs/cfs.c

disk-hello: user/hello.elf host-cfs-put-file
	./tools/cfs_put_file disk.img BIN/HELLO.ELF user/hello.elf
```

## Shell

Adicione `runelf` em `kernel/tools/shell.c` (carrega com `elf_load` + `enter_user`).

```sh
make disk-hello
make run
# no Shell: runelf BIN/HELLO.ELF
```

Serial: `Hello` e o desktop continua.

Tag: `user64-05`.
"""
)

p06 = (
    """# PASSO 06 — fault em user

Tempo: 30 min. Anterior: [PASSO_05.md](PASSO_05.md) · Próximo: [JIT](../fase14-jit64/INDEX.md)

## Objetivo

`#PF` com `cs & 3 != 0` chama `panic_user_fault` (não retorna a user).

### `kernel/metal/irq.c`

```c
if (frame->vector == 14u && (frame->cs & 3u) != 0u) {
    uint64_t cr2;
    __asm__ volatile ("mov %%cr2, %0" : "=r"(cr2));
    panic_user_fault(frame, cr2);
}
```

"""
    + fence_asm("user/fault.asm", ROOT / "user/fault.asm")
    + """```sh
make disk-fault
# Shell: runelf BIN/FAULT.ELF
```

Serial: `user fault rip=... cr2=...` e halt.

Tag: `user64-06`.
"""
)

index = (ROOT / "learn/fase13-usermode64/INDEX.md").read_text(encoding="utf-8")

(OUT / "PASSO_01.md").write_text(p01, encoding="utf-8", newline="\n")
(OUT / "PASSO_02.md").write_text(p02, encoding="utf-8", newline="\n")
(OUT / "PASSO_03.md").write_text(p03, encoding="utf-8", newline="\n")
(OUT / "PASSO_04.md").write_text(p04, encoding="utf-8", newline="\n")
(OUT / "PASSO_05.md").write_text(p05, encoding="utf-8", newline="\n")
(OUT / "PASSO_06.md").write_text(p06, encoding="utf-8", newline="\n")
print("wrote fase13 PASSOs")
