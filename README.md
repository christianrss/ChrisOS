# ChrisOS

Hobby OS ring-0 para QEMU (VBE 640×480): desktop, editor de código no SO e evolução futura com CLVM/ChrisC.

## Build

Precisas de NASM, GCC i686 (`-m32`), `ld`, `objcopy`, QEMU.

```text
make
```

Windows: Git Bash ou WSL para o `makefile` (`cat`, `ld -m elf_i386`). QEMU:

```text
run.bat
```

O rato PS/2 funciona no QEMU moderno com a inicialização PS/2 correta em `boot/input.c`.
