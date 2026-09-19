#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "learn" / "fase14-jit64"
OUT.mkdir(parents=True, exist_ok=True)


def fence(name: str, path: Path) -> str:
    text = path.read_text(encoding="utf-8")
    return f"### `{name}`\n\n```c\n{text}```\n\n"


def fence_sh(name: str, path: Path) -> str:
    text = path.read_text(encoding="utf-8")
    return f"### `{name}`\n\n```c\n{text}```\n\n"


jit_h = ROOT / "compiler/jit/jit.h"
jit_c = ROOT / "compiler/jit/jit.c"
emit_h = ROOT / "compiler/jit/jit_emit.h"
emit_c = ROOT / "compiler/jit/jit_emit.c"
compile_h = ROOT / "compiler/jit/jit_compile.h"
compile_c = ROOT / "compiler/jit/jit_compile.c"
test_c = ROOT / "tools/test_jit_enc.c"
stub_c = ROOT / "tools/jit_host_stub.c"
lp_h = ROOT / "compiler/lang_pipeline.h"
lp_c = ROOT / "compiler/lang_pipeline.c"

p01 = """# PASSO 01 — páginas W^X

Tempo: 40 min. Anterior: [INDEX.md](INDEX.md) · Próximo: [PASSO_02.md](PASSO_02.md)

## Objetivo

Alocar buffer JIT com `pmm_alloc` + `map_4k` writable/NX durante emissão; `jit_seal` remapeia RX (presente, sem W, sem NX).

## Makefile

Acrescente `-Icompiler/jit` em `KINC` e estes objetos em `C_OBJECTS`:

```make
compiler/jit/jit.o compiler/jit/jit_emit.o compiler/jit/jit_compile.o
```

Regra:

```make
compiler/jit/%.o: compiler/jit/%.c
\t$(CC) $(CFLAGS) -c $< -o $@
```

## Arquivos novos

""" + fence("compiler/jit/jit.h", jit_h) + fence("compiler/jit/jit.c", jit_c) + """## Gate

`make` sem warnings. Depois de `jit_seal`, a página não aceita store (W^X).

Tag: `jit64-01`.

## Próximo

[PASSO_02.md](PASSO_02.md)
"""

p02 = """# PASSO 02 — encoder i32

Tempo: 60 min. Anterior: [PASSO_01.md](PASSO_01.md) · Próximo: [PASSO_03.md](PASSO_03.md)

## Objetivo

Encoder x86-64 mínimo: `mov r32, imm`, `add/sub/imul`, `cmp`, `je/jne/jge/jmp`, `call rax`, prologue/epilogue.

Substitua `compiler/jit/jit_emit.h` e `compiler/jit/jit_emit.c` por inteiro:

""" + fence("compiler/jit/jit_emit.h", emit_h) + fence(
    "compiler/jit/jit_emit.c", emit_c
) + fence("tools/jit_host_stub.c", stub_c) + fence(
    "tools/test_jit_enc.c", test_c
) + """## Makefile

```make
host-jit-test: tools/test_jit_enc.c tools/jit_host_stub.c compiler/jit/jit_emit.c
\t$(HOST_CC) -std=c11 -Wall -Wextra -Werror -Icompiler/jit \\
\t\ttools/jit_host_stub.c compiler/jit/jit_emit.c tools/test_jit_enc.c \\
\t\t-o tools/test_jit_enc
\t./tools/test_jit_enc
```

## Gate

`make host-jit-test` imprime `test_jit_enc: ok` (add 2+3 e laço while→10).

Tag: `jit64-02`.

## Próximo

[PASSO_03.md](PASSO_03.md)
"""

p03 = """# PASSO 03 — if / while

Tempo: 50 min. Anterior: [PASSO_02.md](PASSO_02.md) · Próximo: [PASSO_04.md](PASSO_04.md)

## Objetivo

Usar `jit_emit_cmp_r32_imm8`, `jit_emit_jge_rel32`, `jit_emit_jmp_rel32` com patch de rel32 após emitir o corpo (buraco + preenchimento).

O host test `tools/test_jit_enc.c` já cobre o laço `while (i < 10) i++`.

Gate: `make host-jit-test` verde.

Tag: `jit64-03`.

## Próximo

[PASSO_04.md](PASSO_04.md)
"""

p04 = """# PASSO 04 — trampolim SYS

Tempo: 50 min. Anterior: [PASSO_03.md](PASSO_03.md) · Próximo: [PASSO_05.md](PASSO_05.md)

SYS continua em C via `clvm_sys_dispatch`. O JIT chama `jit_sys_trampoline` (id em `edi`).

Substitua `compiler/jit/jit.c` por inteiro (já inclui trampolim + contexto):

""" + fence("compiler/jit/jit.c", jit_c) + """Emissão futura de SYS nativo:

```c
jit_emit_mov_r32_imm(j, 7, sys_id); /* edi */
jit_emit_call_r64(j, (uint64_t)(uintptr_t)jit_sys_trampoline);
```

Antes de cada slice JIT: `jit_set_sys_context(vm, pixels)`.

Tag: `jit64-04`.

## Próximo

[PASSO_05.md](PASSO_05.md)
"""

p05 = """# PASSO 05 — F5 JIT = VM

Tempo: 40 min. Anterior: [PASSO_04.md](PASSO_04.md) · Próximo: [rede](../fase15-net64/INDEX.md)

## Objetivo

Compilar CLVM para buffer selado que chama `clvm_step` via W^X; integrar no pipeline.

Substitua estes arquivos por inteiro:

""" + fence("compiler/jit/jit_compile.h", compile_h) + fence(
    "compiler/jit/jit_compile.c", compile_c
) + fence("compiler/lang_pipeline.h", lp_h) + fence(
    "compiler/lang_pipeline.c", lp_c
) + """### Editor

Shift+F5 → `lang_compile_run_jit`. F5 normal continua VM.

### Shell

Comando `jit SRC/DEMO.CC` compila e corre com JIT.

## Gate

`make host-jit-test` + `make` kernel verdes. QEMU: `jit SRC/DEMO.CC` produz o mesmo visual que `run BIN/DEMO.CLV`.

Tag: `jit64-05`.

## Próximo

[fase15-net64](../fase15-net64/INDEX.md)
"""

index = """# Fase 14 — JIT ChrisC/CLVM → x86-64 W^X

Anterior: [ELF](../fase13-usermode64/INDEX.md). Próximo: [rede](../fase15-net64/INDEX.md).

Buffer executável: `pmm_alloc` + `map_4k` **WRITE** enquanto emite, depois flags **RX** (sem W). Conjunto fechado: encoder i32, if/while, SYS via trampolim, slice JIT = `clvm_step` em buffer selado.

## Ordem

| Passo | Entrega | Gate |
|------:|---------|------|
| [01](PASSO_01.md) | páginas W^X | NX depois de emitir |
| [02](PASSO_02.md) | encoder i32 | host: add/sub/mul |
| [03](PASSO_03.md) | if/while | host: laço 10 |
| [04](PASSO_04.md) | trampolim SYS | pixel via C |
| [05](PASSO_05.md) | F5 JIT = VM | QEMU demo igual |

Comece em [PASSO_01.md](PASSO_01.md).
"""

for name, body in [
    ("PASSO_01.md", p01),
    ("PASSO_02.md", p02),
    ("PASSO_03.md", p03),
    ("PASSO_04.md", p04),
    ("PASSO_05.md", p05),
    ("INDEX.md", index),
]:
    (OUT / name).write_text(body, encoding="utf-8")
    print(f"wrote {name} ({len(body)} bytes)")
