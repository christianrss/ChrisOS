# ChrisOS

Sistema operacional hobby **x86-64** para QEMU: kernel ring-0 em higher-half, bootloader Limine, desktop com janelas, editor, shell, ChrisFS (CFS v3) em disco IDE separado, runtime CLVM/ChrisC e toolchain self-host (ChrisO → ChrisAsm → ChrisLd, KCC).

## Requisitos

| Ferramenta | Uso |
|------------|-----|
| GCC (`-m64`, freestanding) | Kernel e objetos em `build/obj/` |
| NASM (`elf64`) | Stubs de interrupção |
| GNU `ld` (`elf_x86_64`) | Link de `build/iso/boot/kernel.elf` |
| xorriso | ISO híbrida BIOS/UEFI |
| Limine | `third_party/limine` (copiado para staging em build) |
| QEMU `qemu-system-x86_64` | `-M pc`, IDE + virtio-net |
| Python 3 | `tools/cfs_send.py`, geradores de passos |

Windows: use **Git Bash** ou **WSL** para o `makefile` (`mkdir -p`, `cp`, `ld`).

## Build e execução

```bash
make              # build/iso + build/os.iso
make disk.img     # build/disk.img (CFS v3, 512 MiB) se ainda não existir
make run          # QEMU: disco IDE + CD-ROM
make run-stop     # encerra instâncias QEMU
make clean        # remove build/
```

Artefatos gerados (todos sob `build/`):

| Caminho | Conteúdo |
|---------|----------|
| `build/obj/` | Objetos `.o` do kernel (espelham `kernel/`, `compiler/`) |
| `build/iso/` | Staging Limine + `kernel.elf` |
| `build/os.iso` | Imagem bootável |
| `build/disk.img` | Disco de dados CFS (pastas `GAMES/`, `SRC/`, `BIN/`) |
| `build/host/` | Utilitários host (`cfs_put_file`, `cfs_mkdisk`, `kcc`, testes) |
| `build/user/` | Binários flat de teste (`hello.elf`, `fault.elf`) |

Self-host completo (fase 16):

```bash
make seed-selfhost   # popula SYS/ no disco + toolchain no CFS
```

Transferência host→guest (fase 23):

```bash
make disk-put CFS_PATH=GAMES/DEMO.TXT HOST_FILE=/tmp/demo.txt
make send CFS_PATH=SYS/LIVE.C HOST_FILE=foo.c   # TCP :9016, guest rodando
```

Testes host:

```bash
make host-gates
```

## Arquitetura

```text
Limine (BIOS/UEFI)
    └── kernel.elf (higher-half, -mcmodel=kernel)
          ├── metal: GDT/TSS, IDT, PMM, paging, LAPIC/IOAPIC, SMP (BSP+AP)
          ├── gfx: framebuffer 32 bpp, paleta 16, gfx2d, input PS/2
          ├── wm: desktop, taskbar, janelas (TASK_APP para CLVM)
          ├── fs: ATA PIO → ChrisFS v3 (journal, indirect blocks, chmod)
          ├── lang: ChrisC → CLVM; SYS 2D (pixel, sprite, input, ticks)
          ├── net: virtio-net, UDP echo :7, CFS1 TCP :9016 (host xfer)
          └── tools: editor, explorer, shell (`cc`, `mk`, `kcc`, `reboot`)
```

- **ISO** (`build/os.iso`): bootloader + kernel apenas.
- **Disco** (`build/disk.img`): arquivos editáveis; não vai na ISO.
- **CLVM**: VM cooperativa; budget por frame; `SYS` valida argumentos.
- **Compilador**: ChrisC subset → bytecode CLVM; pipeline ChrisO/Asm/Ld no kernel e em `build/host/kcc`.

## QEMU (`make run`)

- Máquina: `pc`, 1 GiB RAM, 2 CPUs
- IDE index 0: `build/disk.img`
- IDE index 2 (CD-ROM): `build/os.iso`
- Rede: `virtio-net-pci`, user mode, forward UDP/TCP `127.0.0.1:7007` → guest `:7`, TCP `127.0.0.1:9016` → guest `:9016`
- Serial: `stdio`

## Layout do repositório

| Diretório | Descrição |
|-----------|-----------|
| `kernel/` | Kernel 64-bit (metal, fs, gfx, wm, lang, net, tools) |
| `compiler/` | ChrisC, CLVM, JIT, ChrisO/Asm/Ld, KCC |
| `tools/` | Utilitários e testes **fonte** (binários em `build/host/`) |
| `iso_root/boot/limine/limine.conf` | Config Limine (fonte; blobs vêm de `third_party/`) |
| `user/` | Programas flat ASM de teste |
| `boot/` | Bootloader legado 32-bit (referência; não usado no path Limine) |
| `third_party/limine` | Binários Limine |
| `build/` | **Gerado** — não versionado |

Material didático local: `learn/` (gitignored).

## Licença

Projeto educacional; consulte os autores para uso fora do contexto de estudo.
