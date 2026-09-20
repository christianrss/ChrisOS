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
| Python 3 | `tools/cfs_send.py` (transferência host→guest) |

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

```bash
make seed-selfhost   # popula SYS/ no disco + toolchain no CFS
make host-gates      # testes host do CFS e toolchain
make disk-put CFS_PATH=GAMES/DEMO.TXT HOST_FILE=/tmp/demo.txt
make send CFS_PATH=SYS/LIVE.C HOST_FILE=foo.c   # TCP :9016, guest rodando
```

## Arquitetura

```text
Limine (BIOS/UEFI)
    └── kernel.elf @ 0xffffffff80000000 (-mcmodel=kernel)
          ├── metal: GDT/TSS, IDT, PMM, paging 4K, LAPIC/IOAPIC, SMP
          ├── gfx: framebuffer Limine 32 bpp, gfx2d, input PS/2
          ├── wm: desktop, taskbar 40px, até 32 janelas
          ├── fs: ATA PIO → cache 16 linhas → ChrisFS v3
          ├── lang: ChrisC → CLVM; SYS 2D/3D na janela App (320×200 hoje; até 1080p — ver fase6-gfxfast64)
          ├── net: virtio-net, UDP echo :7, protocolo CFS1 TCP :9016
          └── tools: editor, explorer, shell (cc, mk, kcc, reboot)
```

- **ISO** (`build/os.iso`): bootloader + kernel apenas — sem arquivos editáveis.
- **Disco** (`build/disk.img`): workspace persistente; montado no boot via ATA PIO.

---

## Especificações técnicas

### CPU e ABI

| Item | Valor |
|------|-------|
| Arquitetura | x86-64, long mode |
| Modelo de kernel | Higher-half, `-mcmodel=kernel` |
| Endereço de link | `0xffffffff80000000` |
| Páginas | 4 KiB (`PMM_PAGE`) |
| Flags de página | `PRESENT`, `WRITE`, `USER`, `NX` (bit 63) |
| Interrupções | IDT 64-bit; PIC legado + LAPIC/IOAPIC (APIC software-disabled por padrão se routing incompleto) |
| Syscalls / usermode | ELF64 flat em `BIN/`; `user_enter` para programas de teste |
| Compilação kernel | `-ffreestanding -fno-pic -fno-pie -fcf-protection=none -mno-red-zone` |

### Memória

| Recurso | Limite | Fonte |
|---------|--------|-------|
| PMM rastreável | até 2 GiB de RAM física | `PMM_MAX_PHYS` |
| Heap do kernel | 64 MiB (16384 páginas) | `HEAP_PAGES` em `heap.c` |
| Stack do kernel (BSP) | 1 MiB | `linker.ld` (`.bss` após `__stack_bottom`) |
| Stacks de AP | 4 páginas (16 KiB) cada, base `0xffffffff92000000` | `smp.h` |
| APs máximos | 8 | `SMP_MAX_APS` |
| HHDM | offset Limine (`bootinfo.hhdm_offset`) | mapeamento físico→virtual |
| Fila de jobs SMP | 32 slots | `JOB_QUEUE_CAP` |

Alocação: PMM para páginas e contíguas; heap bump+free para objetos do kernel (alinhamento 16 bytes, header 16 bytes por bloco).

### Gráficos e desktop

| Item | Valor |
|------|-------|
| Framebuffer | `uint32_t` ARGB, 32 bpp, pitch em bytes (Limine) |
| Resolução máxima suportada | 1920×1080 (`GFX_MAX_WIDTH` × `GFX_MAX_HEIGHT`) |
| Composição | Backbuffer `g_gfx.back`; `Flush` copia para hardware |
| Taskbar | 40 px no topo; ícones do desktop abaixo de y=40 |
| Janelas | até 32 tasks (`TASK_MAX`); título 20 px, 24 caracteres |
| Cor de fundo desktop | `rgb(181, 232, 255)` |
| Jogos CLVM (hoje) | viewport **320×200** na janela `TASK_APP`; blit via `clvm_sys_blit_to` |
| Jogos CLVM (meta T3) | slot escalável até **1920×1080** fullscreen; ver trilha `learn/fase6-gfxfast64/` |
| Paleta jogos 2D | índices 0–15 mapeados para RGB32 |
| Desktop | PIT **60 Hz** (`pit_init(60)`); compositor uma vez por tick |
| 3D no kernel | **planejado** em `kernel/gfx/` — float BSP (`math3d`, `mesh`) + raster int SSE2 nos APs; passos 00–05 |

Programas **não** recebem ponteiro do framebuffer; desenham no buffer do slot ou via SYS.

#### Metas de performance (trilha gfxfast64)

| Tier | Viewport | CPUs QEMU | Meta FPS (cubo 12 tris) | Passos |
|------|----------|-----------|-------------------------|--------|
| **T0** | 320×200 janela App | 1–2 | ≥ 30 | 00–07 |
| **T1** | 640×480 | 2 | **60** | 08–15 |
| **T2** | 1280×720 fullscreen | 4 | ≥ 45 | 16–17 |
| **T3** | **1920×1080** fullscreen | 8 | **60** | 18–21 |

Material didático local (não versionado): `learn/fase6-gfxfast64/INDEX.md` — substitui `fase6-jogos3d`, gfx de `fase7-smp64` e JIT de jogos em `fase14-jit64`.

### ChrisFS (CFS v3)

Disco padrão `build/disk.img`: **512 MiB** = 1 048 576 setores × 512 bytes.

#### Layout on-disk

| Região | LBA início | Setores | Notas |
|--------|------------|---------|-------|
| Superbloco | 0 | 1 | magic `CFS1` (`0x31534643`), versão 3, checksum FNV-like |
| Bitmap | 1 | 256 | 1 bit/setor de dados |
| Tabela de inodes | 257 | 512 | 2048 inodes × 128 bytes |
| Journal | 769 | 64 | replay no mount; até 30 setores por transação |
| Dados | 833 | 1 047 743 | blocos alocados pelo bitmap |

#### Inode e arquivos

| Campo | Valor |
|-------|-------|
| Tamanho do inode | 128 bytes |
| Inodes | 2048 (`CFS_INODE_COUNT`) |
| Ponteiros diretos | 12 blocos |
| Indireção | singly + doubly (128 entradas/nível, `CFS_PTRS_PER_BLOCK`) |
| **Tamanho máximo de arquivo** | **8 460 288 bytes** (~8,06 MiB) = 16 524 blocos × 512 |
| Nome máximo | 64 caracteres (`CFS_NAME_MAX`) |
| Dirent | 80 bytes; 6 entradas/setor |
| Caminho máximo | 512 caracteres, profundidade 32 (`CFS_PATH_MAX`, `CFS_PATH_DEPTH`) |

#### Cache, journal e permissões

| Item | Valor |
|------|-------|
| Cache de setores | 16 linhas LRU por clock (`CFS_CACHE_LINES`) |
| Journal | `JNL_BEGIN` / `JNL_COMMIT`; `JNL_MAX_REC` = 30 |
| Permissões (modo inode) | `READ=1`, `WRITE=2`, `EXEC=4`, `WALK=8` |
| Códigos de erro | `CFS_OK=0`, `EINVAL=-20` … `EPERM=-34` (ver `cfs.h`) |

Pastas seed em `make disk.img`: `GAMES/`, `SRC/`, `BIN/`. `make seed-selfhost` adiciona `SYS/` com toolchain e `BUILD.MK`.

I/O: ATA PIO (primary/secondary, master/slave); sem DMA.

### CLVM e ChrisC

#### CLVM (bytecode)

| Item | Valor |
|------|-------|
| Versão imagem | 1 (`CLVM_VERSION`) |
| Header | 16 bytes |
| Código máximo | 65 535 bytes (`CLVM_MAX_CODE`) |
| RAM da VM | 65 536 bytes (`CLVM_MEMORY_SIZE`) |
| Stack | 256 entradas (`CLVM_STACK_MAX`) |
| Profundidade de call | 64 (`CLVM_CALL_MAX`) |
| Flag `CLVM_FLAG_GAME` | 0x01 — runtime usa buffer do slot (320×200 hoje; escalável até 1080p) |
| Execução | cooperativa; budget por frame (`LANG_VM_BUDGET` = 2000 hoje; ≥ 32000 @ T3); estados `YIELD`, `HALT`, erro |
| JIT | stub (`jit_compile` chama `clvm_step`); lowering real na trilha gfxfast64 passos 13–15 |

#### SYS 2D (ids estáveis)

| id | Stack (antes do id) | Retorno |
|----|---------------------|---------|
| 1 | `x y color` | — |
| 2 | `x y w h color` | — |
| 3 | `x0 y0 x1 y1 color` | — |
| 4 | `addr x y w h key` | — (sprite, color-key) |
| 5 | `addr mapw maph tw th ox oy` | — (tilemap) |
| 6 | `color` | — (clear) |
| 10 | `scancode` | `0/1` (tecla pressionada) |
| 11 | — | `ticks` (uint32 monotônico) |
| 12 | `ms` | — (wait cooperativo) |
| 13 | `freq ms` | — (PC speaker) |

#### SYS 3D e performance (planejados — trilha gfxfast64)

| id | ChrisC | Stack (antes do id) | Retorno |
|----|--------|---------------------|---------|
| 20 | `tri` | `x0 y0 z0 x1 y1 z1 x2 y2 z2 color` | — |
| 21 | `mesh` | `addr vertices triangles angle color` | — |
| 22 | `transform` | `addr mat_addr n_verts` | — |
| 30 | `fps` | — | frames/s estimado |

- Raster 3D: projeção **float** no BSP (`math3d.o`, `mesh.o` com `GFX_FLOAT_CFLAGS`); raster **int** + SSE2 nos APs; z menor = mais perto; vazio = `0xFFFFFFFF`.
- Vértices na RAM CLVM: milli-unidades (`1000` = 1.0f modelo); ChrisC permanece só `int`.
- @ T3: cor + zbuf @ 1080p ≈ 16 MiB por slot; máximo **1 slot Full HD** simultâneo (`gfx_slot_alloc` no heap de 64 MiB).

Endereços `addr` em SYS 4/5/21/22 são **offsets na RAM da VM**, não ponteiros do kernel.

#### ChrisC (fonte `.CC` → `.CLV`)

| Item | Valor |
|------|-------|
| Tipos | `void`, `int` (`i32`); `int v[N]` na trilha gfxfast64 (sem tipo `float`) |
| Controle | `if`/`else`, `while`, `return` |
| Identificadores | até 24 caracteres |
| Símbolos | 128 por compilação |
| AST / tokens | 2048 nós, 4096 tokens |
| Arrays | `v[i]`, `v[i]=x`; endereço = `base + i×4` na RAM CLVM (passo 10) |
| Mat4 via SYS 22 | 16 ints na RAM VM = bits IEEE-754 de `Mat4f`; kernel aplica float no BSP (passos 11–12) |
| Builtins SYS | ids 1–13 (2D), 20–22 (3D), 30 (`fps`) — ver tabelas acima |

#### Toolchain nativa (self-host)

| Componente | Formato / limite |
|------------|------------------|
| ChrisO | magic `CHRISO` (`0x4F524843`), v1; até 256 símbolos, 512 relocs |
| ChrisAsm | texto ChrisO → binário |
| ChrisLd | ELF64 máx. 256 KiB (`CHRISLD_ELF_MAX`) |
| KCC | subset C → ChrisO; saída asm máx. 32 KiB (`KCC_ASM_MAX`) |

Pipeline no guest: `cc arquivo.CC` → CLVM; `mk` lê `SYS/BUILD.MK` via `chrisbuild`. Host: `build/host/kcc`.

### Rede

| Serviço | Porta | Protocolo |
|---------|-------|-----------|
| Echo | 7 (UDP/TCP) | eco de payload |
| CFS1 host transfer | 9016 (TCP) | magic `CFS1`, path ≤512 B, payload, ACK `0x06`/`0x15` |

QEMU user-mode (`make run`): forward `127.0.0.1:7007` → guest `:7`, `127.0.0.1:9016` → guest `:9016`.

Hardware: `virtio-net-pci` com stack mínima in-kernel (sem BSD sockets completos).

### Editor e shell

| Item | Valor |
|------|-------|
| Editor | 256 linhas × 128 colunas; nome de arquivo 512 chars |
| Shell | buffer 512 chars; histórico 8 linhas; painel 48×24 caracteres |
| CWD | paths CFS estilo `GAMES/FOO.CC` |

### QEMU (`make run`)

| Parâmetro | Valor |
|-----------|-------|
| Máquina | `pc` |
| RAM | 1 GiB |
| CPUs | 2 (`-smp 2`) |
| Boot | `order=dc` — disco IDE primeiro, CD-ROM depois |
| Disco dados | IDE index 0 → `build/disk.img` |
| ISO | IDE index 2 (cdrom) → `build/os.iso` |
| Serial | `stdio` |
| Flags | `-no-reboot -no-shutdown` |

---

## Layout do repositório

| Diretório | Descrição |
|-----------|-----------|
| `kernel/` | Kernel 64-bit (`metal`, `fs`, `gfx`, `wm`, `lang`, `net`, `tools`) |
| `compiler/` | ChrisC, CLVM, JIT, ChrisO/Asm/Ld, KCC |
| `tools/` | Utilitários e testes **fonte** (binários em `build/host/`) |
| `iso_root/boot/limine/limine.conf` | Config Limine (fonte; blobs de `third_party/`) |
| `user/` | Programas flat ASM de teste |
| `host/` | Testes host (`test_editor64.c`) |
| `third_party/limine` | Binários Limine |
| `build/` | **Gerado** — não versionado |

Material didático, scripts de debug e geradores de capítulos ficam em `learn/` e padrões listados no `.gitignore` — não são versionados. Trilha ativa de gráficos 3D e performance: `learn/fase6-gfxfast64/` (22 passos, float híbrido BSP + SSE2 int AP, stubs em `stubs/`).

## Licença

Projeto educacional; consulte os autores para uso fora do contexto de estudo.
