#!/usr/bin/env python3
"""Generate learn/fase23-hostxfer/ INDEX + PASSO_01..08 from stubs."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "learn" / "fase23-hostxfer"
STUB = OUT / "stubs"


def rd(name: str) -> str:
    return (STUB / name).read_text(encoding="utf-8")


def fence(lang: str, text: str) -> str:
    return f"```{lang}\n{text.rstrip()}\n```\n\n"


def fence_file(path: str, text: str) -> str:
    ext = path.rsplit(".", 1)[-1]
    lang = {"c": "c", "h": "c", "py": "python", "mk": "make", "txt": "text"}.get(ext, "")
    return f"### `{path}`\n\n{fence(lang, text)}"


def nav(n: int, total: int = 8) -> str:
    prev_l = f"[PASSO_{n-1:02d}.md](PASSO_{n-1:02d}.md)" if n > 1 else "[INDEX.md](INDEX.md)"
    next_l = f"[PASSO_{n+1:02d}.md](PASSO_{n+1:02d}.md)" if n < total else "[INDEX.md](INDEX.md)"
    return f"Tempo: 30–60 min. Anterior: {prev_l} · Próximo: {next_l}\n\n"


INDEX = f"""# Fase 23 — Transferência host↔guest

Anterior: [fase16-selfhost64](../fase16-selfhost64/INDEX.md) (gate nível C).  
Envie arquivos do **host** para o CFS do ChrisOS: offline (`disk.img`) e live (TCP).

## Pré-requisitos

- Fase 16 concluída (`fs backend cfs64`, `make run` estável)
- [fase15-net64](../fase15-net64/INDEX.md): virtio-net + UDP/TCP echo porta 7
- `tools/cfs_put_file` já compila no host

## Por que não FTP?

FTP exige duas conexões TCP, parser de comandos texto e `listen` em várias portas. O protocolo **CFS1** (porta 9016) cabe no TCP mínimo que você já tem.

## Critério gate final (nível C)

1. **Offline:** `make disk-put CFS_PATH=SYS/DEMO.C HOST_FILE=demo.c` → arquivo no explorer
2. **Live:** com `make run`, `make send CFS_PATH=SYS/LIVE.C HOST_FILE=live.c` → serial `xfer: ok SYS/LIVE.C`
3. Boot estável: `fs backend cfs64`, desktop sem panic

## Ordem dos 8 passos

| Passo | Tema | Gate |
|------:|------|------|
| [01](PASSO_01.md) | Entender `cfs_put_file` | put manual no `disk.img` |
| [02](PASSO_02.md) | `make disk-put` | alvo make offline |
| [03](PASSO_03.md) | Protocolo CFS1 + `net_xfer.h` | compila |
| [04](PASSO_04.md) | Estado TCP + header/path | `make all` |
| [05](PASSO_05.md) | Payload + `fs_write` + ACK | serial `xfer: ok` |
| [06](PASSO_06.md) | Integrar `net.c` + makefile | `net: ip=10.0.2.15` |
| [07](PASSO_07.md) | `cfs_send.py` no host | envio live |
| [08](PASSO_08.md) | Gate final | offline + live |

Comece em [PASSO_01.md](PASSO_01.md).

Regenerar: `python3 tools/gen_fase23_hostxfer_passos.py`
"""

PASSOS = []

PASSOS.append(f"""# PASSO 01 — `cfs_put_file` offline

{nav(1)}

## Objetivo

Gravar um arquivo do host **dentro** do `disk.img` (CFS) sem o ChrisOS rodando.

## Contexto

[`tools/cfs_put_file.c`](../../tools/cfs_put_file.c) monta o CFS em modo host e chama `cfs_write`. O QEMU **não pode** ter `disk.img` aberto.

## Pré-requisito: `disk.img` CFS v3

O disco precisa ter **512 MiB** e superbloco `CFS1` versão 3. Se ainda não tem:

{fence('bash', '''make run-stop
make disk.img          # formata v3 (GAMES SRC BIN)
# ou, após fase 16:
make seed-selfhost     # inclui SYS/''')}

## Passos

1. Feche o QEMU: `make run-stop`
2. Crie um arquivo de teste no host:

{fence('bash', 'echo "hello from host" > /tmp/demo.txt')}

3. Grave no disco (use `GAMES/` — existe após `make disk.img`):

{fence('bash', './tools/cfs_put_file disk.img GAMES/DEMO.TXT /tmp/demo.txt')}

4. Suba o OS: `make run` e abra `GAMES/DEMO.TXT` no explorer.

## Saída esperada

```
cfs_put_file: /tmp/demo.txt -> SYS/DEMO.TXT (15 bytes)
```

## Erros comuns

| Sintoma | Causa |
|---------|--------|
| `cannot open disk.img` | QEMU ainda rodando → `make run-stop` |
| `cfs mount failed (-22)` | disco sem CFS v3 → `make disk.img` |
| `tem N bytes; precisa 536870912` | imagem 16 MiB (v2) ou vazia → `make disk.img` |
| `cfs_write failed (-23)` | pasta pai não existe → use `GAMES/` ou `make seed-selfhost` |

Tag: `hostxfer-01`.
""")

PASSOS.append(f"""# PASSO 02 — alvo `make disk-put`

{nav(2)}

## Objetivo

Atalho no [`makefile`](../../makefile) para o fluxo offline.

## O que colar

Acrescente ao [`makefile`](../../makefile):

{fence('make', rd('disk_put.mk'))}

Altere a regra `run` para **não** recriar `disk.img` a cada boot (se ainda não fez):

{fence('make', '''run: $(ISO) run-stop
	@test -f disk.img || $(MAKE) disk.img
	@sleep 1''')}

## Como verificar

{fence('bash', '''make run-stop
make disk-put CFS_PATH=SYS/DEMO.TXT HOST_FILE=/tmp/demo.txt
make run''')}

Tag: `hostxfer-02`.
""")

PASSOS.append(f"""# PASSO 03 — protocolo CFS1 e `net_xfer.h`

{nav(3)}

## Objetivo

Definir o protocolo binário **CFS1** na porta TCP **9016**.

## Layout do pacote

```
[magic "CFS1"][path_len u32 BE][file_size u32 BE][path][file bytes]
→ 1 byte: 0x06 OK | 0x15 erro
```

Limites: `path_len < 512`, `file_size ≤ CFS_MAX_FILE_SIZE` (8 460 288).

## Arquivo novo

Copie [`stubs/net_xfer.h`](stubs/net_xfer.h) para [`kernel/net/net_xfer.h`](../../kernel/net/net_xfer.h):

{fence('c', rd('net_xfer.h'))}

## Como verificar

Ainda não ligue o módulo — só confirme que o header existe. `make all` deve continuar verde.

Tag: `hostxfer-03`.
""")

PASSOS.append(f"""# PASSO 04 — `net_xfer.c` (header + path)

{nav(4)}

## Objetivo

Criar [`kernel/net/net_xfer.c`](../../kernel/net/net_xfer.c) com máquina de estados TCP e parse de header/path.

## Referência

Copie o stub completo de [`stubs/net_xfer.c`](stubs/net_xfer.c) para `kernel/net/net_xfer.c`.

{fence_file('kernel/net/net_xfer.c', rd('net_xfer.c'))}

## Makefile

**Ainda não** acrescente `kernel/net/net_xfer.o` em `C_OBJECTS` — o link falha até o PASSO 06 exportar `net_tcp_xmit` em `net.c`.

## Como verificar

Leia o código e entenda `XferPhase`: `HDR → PATH → DATA → DONE`. `make all` continua verde (arquivo criado mas não linkado).

Tag: `hostxfer-04`.
""")

PASSOS.append(f"""# PASSO 05 — payload, `fs_write` e ACK

{nav(5)}

## Objetivo

Completar `xfer_consume()` e `xfer_finish()`: `kmalloc`, `fs_write`, `serial_puts("xfer: ok ...")`, ACK `0x06`/`0x15`.

O stub em [`stubs/net_xfer.c`](stubs/net_xfer.c) já contém a implementação completa — se colou no PASSO 04, revise:

- `path_upper()` — CFS usa paths em maiúsculas
- `xfer_send_ack()` — um byte de resposta
- liberação com `kfree` em `xfer_reset()`

## Teste parcial (após PASSO 06)

Com o guest rodando e porta 9016 aberta, `python3 tools/cfs_send.py` deve funcionar.

Tag: `hostxfer-05`.
""")

PASSOS.append(f"""# PASSO 06 — integrar em `net.c` e QEMU

{nav(6)}

## Objetivo

Rotear TCP porta **9016** para `net_xfer_tcp()` e expor `net_tcp_xmit()`.

## Patch em `net.c`

Siga [`stubs/net_c_patch.txt`](stubs/net_c_patch.txt):

{fence('text', rd('net_c_patch.txt'))}

## Makefile

1. `kernel/net/net_xfer.o` em `C_OBJECTS` (se ainda não)
2. Variável e hostfwd:

{fence('make', rd('makefile_snippets.mk'))}

Na linha `$(QEMU)` de `run`, acrescente ao `-netdev user`:

```
hostfwd=tcp:127.0.0.1:$(HOST_XFER_PORT)-:9016
```

## Como verificar

{fence('bash', 'make run')}

Serial deve mostrar `net: ip=10.0.2.15` e `virtio-net ready`.

Tag: `hostxfer-06`.
""")

PASSOS.append(f"""# PASSO 07 — `cfs_send.py` no host

{nav(7)}

## Objetivo

Script Python que envia CFS1 para `127.0.0.1:9016`.

## Arquivo novo

Copie [`stubs/cfs_send.py`](stubs/cfs_send.py) para [`tools/cfs_send.py`](../../tools/cfs_send.py):

{fence_file('tools/cfs_send.py', rd('cfs_send.py'))}

## Como verificar

Terminal 1: `make run`  
Terminal 2:

{fence('bash', '''echo "live test" > /tmp/live.txt
python3 tools/cfs_send.py SYS/LIVE.TXT /tmp/live.txt''')}

Serial do guest: `xfer: ok SYS/LIVE.TXT`

Tag: `hostxfer-07`.
""")

PASSOS.append(f"""# PASSO 08 — gate final

{nav(8)}

## Objetivo

Alvo `make send` e checklist offline + live.

## Makefile

{fence('make', '''send:
	@test -n "$(CFS_PATH)" || (echo "usage: make send CFS_PATH=SYS/FILE.C HOST_FILE=foo.c" && exit 1)
	@test -n "$(HOST_FILE)" || (echo "usage: make send CFS_PATH=SYS/FILE.C HOST_FILE=foo.c" && exit 1)
	python3 tools/cfs_send.py $(CFS_PATH) $(HOST_FILE)''')}

## Checklist gate nível C

- [ ] `make run-stop && make disk-put CFS_PATH=SYS/OFF.C HOST_FILE=off.c && make run` → arquivo no CFS
- [ ] Com QEMU rodando: `make send CFS_PATH=SYS/LIVE.C HOST_FILE=live.c` → `xfer: ok` na serial
- [ ] `fs backend cfs64` no boot
- [ ] Desktop estável (sem `PANIC` / `#GP`)

## Próximo

Volte ao [INDEX.md](INDEX.md). Opcional: guest→host (fase futura).

Tag: `hostxfer-08`.
""")


def main() -> None:
    OUT.mkdir(parents=True, exist_ok=True)
    STUB.mkdir(parents=True, exist_ok=True)
    (OUT / "INDEX.md").write_text(INDEX, encoding="utf-8")
    for i, body in enumerate(PASSOS, start=1):
        (OUT / f"PASSO_{i:02d}.md").write_text(body, encoding="utf-8")
    print(f"Wrote INDEX + {len(PASSOS)} passos in {OUT}")


if __name__ == "__main__":
    main()
