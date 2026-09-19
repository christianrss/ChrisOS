#!/usr/bin/env python3
"""Generate learn/fase16-selfhost64/ INDEX + PASSO_01..22."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "learn" / "fase16-selfhost64"
STUB = OUT / "stubs"


def rd(name: str) -> str:
    return (STUB / name).read_text(encoding="utf-8")


def fence(lang: str, text: str) -> str:
    return f"```{lang}\n{text.rstrip()}\n```\n\n"


def fence_file(path: str, text: str) -> str:
    return f"### `{path}`\n\n{fence('c' if path.endswith('.c') or path.endswith('.h') else 'make', text)}"


def nav(n: int, total: int = 22) -> str:
    prev_l = f"[PASSO_{n-1:02d}.md](PASSO_{n-1:02d}.md)" if n > 1 else "[INDEX.md](INDEX.md)"
    next_l = f"[PASSO_{n+1:02d}.md](PASSO_{n+1:02d}.md)" if n < total else "[INDEX.md](INDEX.md)"
    return f"Tempo: 45–90 min. Anterior: {prev_l} · Próximo: {next_l}\n\n"


INDEX = """# Fase 16 — Self-host kernel (expansão + toolchain)

Anterior: [fase15-net64](../fase15-net64/INDEX.md).  
Esta pasta cobre **todos** os marcos 16–22 do roadmap self-host em **22 passos**.

## Pré-requisitos

- [fase7-smp64](../fase7-smp64/INDEX.md) lida (AP workers)
- [fase9](../fase9-cfs64/INDEX.md)–[fase15](../fase15-net64/INDEX.md) verdes no seu tree
- `disk.img` CFS **v2** (16 MiB) ou migrado; após PASSO 06 recrie/migre para **v3** (512 MiB)

## Critério nível C (gate final)

Dentro do QEMU, **sem `gcc` no PC** (exceto um `make seed-selfhost` no host):

1. Árvore `SYS/KERNEL/`, `SYS/TOOLS/`, `BIN/` no CFS
2. Shell: `mk kernel` → `BIN/KERNEL.ELF`
3. `reboot` carrega o novo kernel (Limine + disco)
4. Serial: banner `selfhost=1`

ChrisC (`cc`, jogos CLVM) ≠ KCC (`kcc`, C do kernel).

## Bootstrap (uma vez no host)

```mermaid
flowchart LR
  host[make seed-selfhost]
  sys[SYS tree no CFS]
  kcc0[BIN/KCC.ELF]
  host --> sys
  host --> kcc0
```

## Ordem dos 22 passos

| Passo | Marco | Entrega | Gate |
|------:|-------|---------|------|
| [01](PASSO_01.md) | 16 | CFS v3 geometria | compila |
| [02](PASSO_02.md) | 16 | dirent 80 / nome 64 | encode round-trip |
| [03](PASSO_03.md) | 16 | paths 512/32 | `host-cfs-paths-test` |
| [04](PASSO_04.md) | 16 | rename cross-dir | paths test |
| [05](PASSO_05.md) | 16 | truncate sem `work[]` BSS | `host-cfs-test` ~8 MiB max |
| [06](PASSO_06.md) | 16 | migrate v2→v3 + `make disk` | `host-cfs-migrate-v2v3` |
| [07](PASSO_07.md) | 16 | heap 64 MiB, PMM 2 GiB | `make iso` |
| [08](PASSO_08.md) | 16 | editor/shell 512 chars | QEMU save `SYS/` |
| [09](PASSO_09.md) | 17 | APIC + 2 CPUs | `cpu_online_count=2` |
| [10](PASSO_10.md) | 17 | fila jobs | stress serial |
| [11](PASSO_11.md) | 17 | `job_submit` compilação | 2 jobs paralelos |
| [12](PASSO_12.md) | 18 | formato ChrisO | `host-chriso-test` |
| [13](PASSO_13.md) | 18 | ChrisAsm | asm → ChrisO |
| [14](PASSO_14.md) | 18 | ChrisLd | ELF serial |
| [15](PASSO_15.md) | 19 | KCC-0 + `serial.c` | `BIN/KCC.ELF` |
| [16](PASSO_16.md) | 19 | KCC-1 `port.c` | `kcc` in-OS |
| [17](PASSO_17.md) | 19 | KCC self-compile | KCC→KCC |
| [18](PASSO_18.md) | 20 | `SYS/BUILD.MK` | parse host |
| [19](PASSO_19.md) | 20 | `mk kernel` | `BIN/KERNEL.ELF` |
| [20](PASSO_20.md) | 21 | Limine + reboot | banner novo |
| [21](PASSO_21.md) | 21 | manifest kernel completo | sem gcc |
| [22](PASSO_22.md) | 22 | `seed-selfhost` + INDEX | gate nível C |

Comece em [PASSO_01.md](PASSO_01.md).

Regenerar passos: `python3 tools/gen_fase16_passos.py`
"""

PASSOS = []

# --- PASSO 01 ---
PASSOS.append(f"""# PASSO 01 — geometria CFS v3

{nav(1)}

## Objetivo

Disco **512 MiB**, versão on-disk **3**, journal em LBA fixo, data area depois do journal. Corrige volumes onde `CFS_JOURNAL_LBA` era usado em `cfs.c` mas não definido em `storage_limits.h`.

## Por que

Self-host precisa de `SYS/KERNEL/` com centenas de arquivos `.c`, objetos ChrisO e `BIN/KERNEL.ELF`. CFS v2 (16 MiB, 128 inodes) enche rápido.

## Arquivo

Substitua [kernel/fs/storage_limits.h](kernel/fs/storage_limits.h) por inteiro:

{fence_file('kernel/fs/storage_limits.h', rd('storage_limits_v3.h'))}

## O que não tocar

`cfs.c`, `cfs_format.h` — ainda v2 até PASSO 02.

## Como verificar

```sh
make kernel/fs/cfs.o
```

`CFS_DATA_LBA` deve ser **833** (`769 + 64`). `CFS_VERSION` = 3.

Tag: `sh16-01`.

## Próximo

[PASSO_02.md](PASSO_02.md)
""")

# --- PASSO 02 ---
PASSOS.append(f"""# PASSO 02 — dirent 80 bytes, nome 64

{nav(2)}

## Objetivo

`CFS_DIRENT_SIZE` 80 (8 + 64 + padding), 6 entradas por setor. `cfs_dirent_encode`/`decode` continuam em [kernel/fs/cfs_format.h](kernel/fs/cfs_format.h).

## Por que

Nomes de 24 caracteres não cabem `PORT.C` + caminhos longos em `SYS/KERNEL/METAL/`.

## O que colar

Em `kernel/fs/cfs_format.h`, `CfsDirent` já usa `name[CFS_NAME_MAX]` — após PASSO 01 só confirme:

```c
typedef struct CfsDirent {{
    uint32_t inode;
    uint8_t type;
    uint8_t name_len;
    uint16_t flags;
    uint8_t name[CFS_NAME_MAX];
}} CfsDirent;
```

Em `cfs_super_decode`, aceite **apenas** `CFS_VERSION` 3:

```c
    if (cfs_get16(in + 4) != CFS_VERSION) return -2;
```

## Como verificar

```sh
python3 -c "print(512//80, 8+64)"
```

Deve imprimir `6 72`. Rode `make host-cfs-test` — falhará até PASSO 06 atualizar testes; compile apenas.

Tag: `sh16-02`.
""")

# --- PASSO 03 ---
PASSOS.append(f"""# PASSO 03 — paths 512 / profundidade 32

{nav(3)}

## Objetivo

`CFS_PATH_MAX` 512, `CFS_PATH_DEPTH` 32. `PathParts` em `cfs.c` usa arrays `[CFS_PATH_DEPTH]`.

## O que colar

### `kernel/fs/cfs.h`

```c
#define CFS_PATH_MAX 512u
#define CFS_PATH_DEPTH 32u
```

### `kernel/fs/cfs.c` — struct `PathParts`

```c
typedef struct PathParts {{
    uint32_t ncomp;
    uint32_t start[CFS_PATH_DEPTH];
    uint32_t len[CFS_PATH_DEPTH];
    const char *s;
    uint32_t total;
}} PathParts;
```

## Como verificar

Após PASSO 06: `make host-cfs-paths-test`.

Tag: `sh16-03`.
""")

# --- PASSO 04 ---
PASSOS.append(f"""# PASSO 04 — rename entre diretórios

{nav(4)}

## Objetivo

`cfs_rename` move `GAMES/C.TXT` → `SRC/C.TXT` sem `CFS_EXDEV`.

## O que colar

Em `cfs_rename` ([kernel/fs/cfs.c](kernel/fs/cfs.c)), **remova**:

```c
    if (pa != pb) return CFS_EXDEV;
```

E **adicione** permissão de escrita no pai de destino quando `pa != pb`:

```c
    {{
        CfsInode parent_in;
        rc = inode_read(fs, pa, &parent_in);
        if (rc != CFS_OK) return rc;
        rc = cfs_perm_need(&parent_in, CFS_PERM_WRITE);
        if (rc != CFS_OK) return rc;
        if (pa != pb) {{
            rc = inode_read(fs, pb, &parent_in);
            if (rc != CFS_OK) return rc;
            rc = cfs_perm_need(&parent_in, CFS_PERM_WRITE);
            if (rc != CFS_OK) return rc;
        }}
    }}
```

(substitua o bloco que só checava `pa`.)

## Como verificar

Em `tools/test_cfs_paths.c`, espere `CFS_OK` em rename cross e leitura em `SRC/C.TXT`.

Tag: `sh16-04`.
""")

# --- PASSO 05 ---
PASSOS.append(f"""# PASSO 05 — buffer de truncate no heap

{nav(5)}

## Objetivo

Remover `uint8_t work[CFS_MAX_FILE_SIZE]` de `struct Cfs` — 16 MiB no BSS quebraria o kernel.

## O que colar

### `kernel/fs/cfs.h` — remova a linha `work[...]` de `struct Cfs`.

### `kernel/fs/cfs.c` — topo (após includes):

```c
#ifdef __freestanding__
#include "heap.h"
#else
#include <stdlib.h>
#endif

static void *cfs_work_alloc(uint32_t size) {{
#ifdef __freestanding__
    return kmalloc((uint64_t)size);
#else
    return malloc((size_t)size);
#endif
}}

static void cfs_work_free(void *p) {{
    if (!p) return;
#ifdef __freestanding__
    kfree(p);
#else
    free(p);
#endif
}}
```

### `cfs_truncate` — aloque `CFS_MAX_FILE_SIZE`, leia, escreva, `cfs_work_free`.

## Como verificar

`make host-cfs-test` com arquivo de **8460288 bytes** (~8,06 MiB, `CFS_MAX_FILE_SIZE` geométrico).

Tag: `sh16-05`.
""")

# --- PASSO 06 ---
PASSOS.append(f"""# PASSO 06 — migração v2→v3 e disco 512 MiB

{nav(6)}

## Objetivo

Testes host com `malloc` para disco 512 MiB; ferramenta `cfs_migrate_v2v3`; alvo `make disk`.

## Arquivos novos

Copie de [learn/fase16-selfhost64/stubs/test_disk.h](stubs/test_disk.h) para `tools/test_disk.h`:

{fence('c', rd('test_disk.h'))}

Crie `tools/cfs_migrate_v2v3.c`:

{fence_file('tools/cfs_migrate_v2v3.c', rd('cfs_migrate_v2v3.c'))}

## Makefile

```make
HOST_CFLAGS := ... -Itools

host-cfs-migrate-v2v3: tools/cfs_migrate_v2v3.c kernel/fs/cfs.c
	$(HOST_CC) $(HOST_CFLAGS) -Ikernel/fs \\
		-o tools/cfs_migrate_v2v3 tools/cfs_migrate_v2v3.c kernel/fs/cfs.c

disk:
	dd if=/dev/zero of=disk.img bs=1M count=512 status=none
```

Atualize cada `tools/test_cfs_*.c`: `#include "test_disk.h"`, `test_disk_init()` no `main`, `g_disk` via ponteiro global do header.

## Como verificar

```sh
make host-cfs-test host-cfs-paths-test host-cfs-journal-test
make host-cfs-migrate-v2v3
./tools/cfs_migrate_v2v3 disk_v2.img disk_v3.img
```

Journal test: `CFS_DATA_LBA` = **833**.

Tag: `sh16-06`.
""")

# --- PASSO 07 ---
PASSOS.append(f"""# PASSO 07 — heap, PMM e QEMU

{nav(7)}

## Objetivo

RAM para toolchain: heap **64 MiB**, PMM até **2 GiB**, QEMU **1 GiB** e **2 CPUs**.

## O que colar

### `kernel/metal/heap.c`

```c
#define HEAP_PAGES     16384ull
```

### `kernel/metal/pmm.h`

```c
#define PMM_MAX_PHYS (2048ull * 1024ull * 1024ull)
```

### `makefile` alvo `run`

```make
	$(QEMU) -M q35 -m 1G -smp 2 -boot d -cdrom $(ISO) \\
```

## Como verificar

```sh
make iso
```

Serial no boot: heap init sem panic.

Tag: `sh16-07`.
""")

# --- PASSO 08 ---
PASSOS.append(f"""# PASSO 08 — caminhos longos na UI

{nav(8)}

## Objetivo

Editor, Explorer, shell e `lang_pipeline` alinhados com `CFS_PATH_MAX`.

## O que colar

| Arquivo | Constante |
|---------|-----------|
| `kernel/fs/fs.h` | `#define FS_PATH 512` |
| `kernel/tools/editor.h` | `#define ED_NAME 512` |
| `kernel/wm/task.h` `ExplorerState.cwd` | `char cwd[512]` |
| `kernel/tools/shell.c` | `#define SH_LINE 512`, `g_cwd[512]` |
| `compiler/lang_pipeline.c` | `#define LANG_SOURCE_MAX 262144` |

Em `explorer.c`, troque literais `96` por `CFS_PATH_MAX` (inclua `cfs.h`).

## Como verificar

QEMU: salve `SYS/KERNEL/METAL/PORT.C` com caminho longo; Explorer abre a pasta.

Tag: `sh16-08`.
""")

# --- PASSO 09-11 SMP ---
PASSOS.append(f"""# PASSO 09 — APIC e segundo CPU

{nav(9)}

## Objetivo

`cpu_online_count >= 2`. Siga [fase7 PASSO 01–03](../fase7-smp64/PASSO_03.md) se ainda não fez.

## Makefile

```make
	kernel/metal/apic.o kernel/metal/ioapic.o \\
```

## Como verificar

Serial: `cpu_online_count=2` após boot.

Tag: `sh16-09`.
""")

PASSOS.append(f"""# PASSO 10 — fila de jobs

{nav(10)}

## Objetivo

Implemente [fase7 PASSO 05](../fase7-smp64/PASSO_05.md) (`kernel/metal/job.c`, `job.h`) se ausente.

## Gate

`job_submit` + `job_wait_idle`: contador partilhado exato com 2 CPUs.

Tag: `sh16-10`.
""")

PASSOS.append(f"""# PASSO 11 — jobs para compilação

{nav(11)}

## Objetivo

API `kcc_job_submit_path(const char *path)` que enfileira compilação de um `.c` (stub aceita contar jobs).

## Como verificar

Shell ou serial: dispare 2 jobs dummy; ambos completam.

Tag: `sh16-11`.
""")

# --- PASSO 12-14 toolchain stubs ---
PASSOS.append(f"""# PASSO 12 — formato objeto ChrisO

{nav(12)}

## Objetivo

Formato próprio `.o` ChrisO: seções text/rodata/data, símbolos, relocs PC32.

## Arquivos novos

- `compiler/chrisld/chriso.h`
- `compiler/chrisld/chriso.c`

{fence_file('compiler/chrisld/chriso.h', rd('chriso.h'))}
{fence_file('compiler/chrisld/chriso.c', rd('chriso.c'))}

## Makefile

```make
host-chriso-test: tools/test_chriso.c compiler/chrisld/chriso.c
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -Icompiler/chrisld \\
		tools/test_chriso.c compiler/chrisld/chriso.c -o tools/test_chriso
	./tools/test_chriso
```

Tag: `sh16-12`.
""")

PASSOS.append(f"""# PASSO 13 — ChrisAsm

{nav(13)}

## Objetivo

Assembler subset x86-64: `.text`, labels, `mov`, `push`, `pop`, `ret`, `int`.

## Arquivo

`compiler/chrisasm/chrisasm.c` + `chrisasm.h` — entrada texto, saída ChrisO.

Tag: `sh16-13`.
""")

PASSOS.append(f"""# PASSO 14 — ChrisLd

{nav(14)}

## Objetivo

Linker: N objetos ChrisO → ELF64 freestanding em `0xffffffff80000000`.

## Arquivo

`compiler/chrisld/chrisld.c`

## Gate

`host-chrisld-test`: `hello.o` imprime no serial quando executado no host (ou valida ELF magic).

Tag: `sh16-14`.
""")

# --- PASSO 15-17 KCC ---
PASSOS.append(f"""# PASSO 15 — KCC-0 e bootstrap no disco

{nav(15)}

## Objetivo

Compilador mínimo `compiler/kcc/kcc.c`: `void`, `int`, `if/while`, funções, emite ChrisO. Host compila `KCC0` → `tools/kcc_put_disk` grava `BIN/KCC.ELF`.

## Gate

KCC compila `kernel/metal/serial.c` no host para ChrisO.

Tag: `sh16-15`.
""")

PASSOS.append(f"""# PASSO 16 — KCC-1 ponteiros e port.c

{nav(16)}

## Objetivo

Estender KCC: ponteiros, `struct`, `static`, `extern`. Compilar `port.c` in-OS.

## Shell

Novo comando `kcc PATH` (distinto de `cc` ChrisC).

Tag: `sh16-16`.
""")

PASSOS.append(f"""# PASSO 17 — KCC compila a si mesmo

{nav(17)}

## Objetivo

KCC-1 compila `compiler/kcc/kcc.c` → novo `BIN/KCC.ELF` (bootstrap TCC-style).

Tag: `sh16-17`.
""")

# --- PASSO 18-21 build ---
PASSOS.append(f"""# PASSO 18 — manifest SYS/BUILD.MK

{nav(18)}

## Objetivo

Lista de `C_OBJECTS` e `ASM_OBJECTS` espelhando o `makefile` do host.

## Arquivo

Conteúdo inicial (copie para `SYS/BUILD.MK` no CFS):

{fence('', rd('SYS_BUILD.MK'))}

Tag: `sh16-18`.
""")

PASSOS.append(f"""# PASSO 19 — chrisbuild e mk kernel

{nav(19)}

## Objetivo

`kernel/tools/chrisbuild.c` lê `SYS/BUILD.MK`, chama KCC + ChrisAsm + ChrisLd.

## Shell

`mk kernel`, `mk clean`, `mk install` → `BIN/KERNEL.ELF`.

Tag: `sh16-19`.
""")

PASSOS.append(f"""# PASSO 20 — boot do KERNEL.ELF no disco

{nav(20)}

## Objetivo

`iso_root/boot/limine/limine.conf` aponta para kernel no IDE; `mk install` copia ELF; comando `reboot`.

Tag: `sh16-20`.
""")

PASSOS.append(f"""# PASSO 21 — manifest kernel completo

{nav(21)}

## Objetivo

Todos os `.c` e `.asm` do `makefile` host no `BUILD.MK`. `mk kernel` sem `gcc` no PC.

Tag: `sh16-21`.
""")

# --- PASSO 22 ---
PASSOS.append(f"""# PASSO 22 — seed-selfhost e gate nível C

{nav(22)}

## Objetivo

Último passo: alvo `make seed-selfhost` no host copia `SYS/` + ferramentas; atualize documentação global.

## Makefile (host)

```make
seed-selfhost: iso disk host-cfs-put-file user/kcc0.elf
	./tools/cfs_put_file disk.img SYS/BUILD.MK learn/fase16-selfhost64/stubs/SYS_BUILD.MK
	# ... demais fontes SYS/KERNEL/
```

## Atualizar [learn/INDEX.md](../INDEX.md)

Adicione na tabela de fases:

```markdown
| 16 | [fase16-selfhost64](fase16-selfhost64/INDEX.md) | **ativo** | CFS v3 + self-host kernel |
```

## Atualizar [learn/ARQUITETURA_64.md](../ARQUITETURA_64.md)

Na tabela de fases, linha 16:

```markdown
| 16 | [fase16-selfhost64](fase16-selfhost64/INDEX.md) | KCC + ChrisLd + mk kernel |
```

## Gate final

Clone limpo → `make seed-selfhost` → QEMU → editar `SYS/KERNEL/METAL/SERIAL.C` → `mk kernel` → `reboot` → mensagem nova na serial.

Tag: `sh16-22`.
""")


def main() -> None:
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT / "INDEX.md").write_text(INDEX, encoding="utf-8")
    for i, body in enumerate(PASSOS, start=1):
        (OUT / f"PASSO_{i:02d}.md").write_text(body, encoding="utf-8")
    print(f"wrote INDEX + {len(PASSOS)} passos to {OUT}")


if __name__ == "__main__":
    main()
