# Testes

```text
make -C chrisvm
make -C chrisvm test
make chrisvm
make chrisvm-test
```

`make chrisvm-test` compila o interpretador, liga `guests/arith.asm` e `guests/splash.asm`, roda `test_chrisvm`, exige a linha `OK` do guest aritmético e a linha `splash` do guest gráfico, e grava `build/chrisvm/splash.png`.

## O que a suíte cobre

| Teste | O que trava |
| --- | --- |
| flags | `~0 + 1` e overflow com sinal: resultado, CF, ZF, SF, OF, PF, AF |
| ADD na CPU | `RAX = -1 + 1`, halt, CF, ZF |
| memória e CALL | `10+20`, store/load em `0x4000`, `CALL`/`RET`, `RAX = 31` |
| serial | loopback `0xAE` em `0x3FC`/`0x3F8` |
| porta livre | `IN` de `0x80` devolve `0xFF` |
| shutdown | `OUT` `0x01` na porta `0x501` |
| `#UD` | `0F 0B` |
| `#PF` | load de `0x01000000`, `CR2` igual |
| `#GP` | load não canônico |
| limite de passos | `JMP` curto para si |
| `#DE` | divisão por zero e quociente de 8 bits que não cabe |
| IDIV 8 | `-8 / 2` deixa `AL = -4` |
| MUL 64 | `~0 * 2` preenche RDX e CF |
| CPUID | vendor `ChrisCPU` na folha 0, também pela instrução |
| MSR | escrita e leitura de `EFER = 0x100`; MSR `0x123` gera `#GP` |
| ELF | o guest `arith.elf` imprime `OK`, halt, `RAX = 31` |
| ELF ruim | magic inválido e header truncado |
| fuzz | 2000 buffers no decoder, sem máquina |
| MMIO | PDE de 2 MiB em `0x06000000`, store e load de `0x2A` |
| `REP STOSD` | quatro pixels `0x12345678` em `0x02000000`, `RCX = 0`, halt |
| splash | serial `splash`, fundo `0x00101828`, painel `0x00141C2C`, barra `0x004C8DFF`, tela suja |
| físico vazio | PDE sem dispositivo gera `UNMAPPED` |
| pilha | `PUSH` com RSP em página ausente gera `#PF` |
| ChrisHV | `create` devolve nulo |

O guest `arith.asm` é o marco M1: serial, aritmética, comparação, memória, chamada e `HLT`, sem QEMU.

O guest `splash.asm` pinta o framebuffer linear e executa `HLT`. O PNG esperado tem o título `CHRISOS`, o subtítulo `inicializando`, a barra azul e o rodapé `CHRISVM`. Esse quadro não é o `gfx_present` do kernel.

Não há ainda teste de `INT` com IDT programada, nem comparação diferencial contra o QEMU. O QEMU permanece ferramenta externa. A suíte do ChrisVM não liga com ele.
