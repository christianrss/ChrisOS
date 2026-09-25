# Censo do kernel ChrisOS

Fonte: `objdump -d` de `build/iso/boot/kernel.elf`. 177 mnemônicos AT&T, 198562 instruções na seção `.text`. Prefixos (`lock`, `rep`, `data16`, `cs`) aparecem como tokens separados. `movl`, `movb` e `movq` são formas de `MOV`.

O early path de `kstart` (`ffffffff80001000`) é `PUSH`, `MOV`, `SUB`, `CALL`, `XOR`, `TEST`, `JE`, `CLI`, `HLT`, `JMP`, e em seguida a serial usa `IN`/`OUT`. `GDT` usa `LGDT` e `LRETQ`. `IDT` usa `LIDT`. Syscall de guest é `INT 0x80` e `IRETQ`. O binário do kernel não contém `CPUID`, `WRMSR`, `RDMSR`, `SYSCALL`, `SYSRET` nem `SWAPGS`. O `CPUID` de `sse_init.c` está no caminho de host.

| Instrução ou traço | Vezes no kernel | Boot | Runtime | ChrisCPU |
| --- | --- | --- | --- | --- |
| MOV e formas `mov*` de inteiro | 83193 + formas menores | sim | sim | sim, testado no guest e na suíte |
| CALL | 12518 | sim | sim | sim, guest |
| JMP | 9635 | sim | sim | sim |
| ADD / SUB | 9452 / 3211 | sim | sim | sim |
| LEA | 7791 | sim | sim | sim |
| TEST / CMP / Jcc | milhares | sim | sim | sim para as condições de 0 a 15 |
| PUSH / POP | 3689 / 763 | sim | sim | sim |
| SHL / SHR / SAR | 2869 / 443 / 124 | não no primeiro bloco | sim | sim |
| MOVSX / MOVZX (`movslq`, `cltq`, `movzbl`) | milhares | sim, `movzbl` na serial | sim | sim |
| RET / LEAVE | 2279 / 1950 | sim | sim | sim |
| IMUL / MUL / DIV / IDIV | 1329 / 11 / 22 / 14+ | não | sim | sim, IDIV 8 e DIV 64 testados; o resto da matriz não |
| XOR / AND / OR | centenas | sim, `xor` | sim | sim |
| IN / OUT | 4 / 4 | sim | sim | sim, serial |
| NOP | 1297 | não | sim | sim |
| XCHG | 77 | não | sim | sim, sem teste próprio |
| CLI / STI / HLT / CLD | 9 / 4 / 12 / 2 | sim | sim | sim |
| LGDT / LIDT | 2 / 1 | sim | não | decodificado e executado, sem teste de guest |
| LTR / STR | 2 / 1 | sim | não | não |
| LRETQ | 2 | sim | não | não |
| IRETQ | 3 | não | sim | decodificado, sem teste com IDT |
| INT | stubs e syscall | não | sim | entrega com IDT vazia volta ao monitor |
| PUSHF | 2 | não | sim | sim, sem teste |
| LOCK | 7 | não | sim | memória é sequencial; registrador é `#UD` |
| INVLPG | 4 | não | sim | não |
| RDTSC | 2 | não | sim | não. O TSC interno só conta instruções |
| RDRAND / PAUSE / MFENCE | 1 / 30 / 1 | não | sim | não |
| SSE escalar (`movss`, `addss`, `mulss`, …) | milhares no conjunto | não | sim | não. CPUID não anuncia SSE |
| x87 (`flds`, `fstps`, `fucomip`, …) | dezenas | não | sim | não |
| FXSAVE / FXRSTOR | 1 / 1 | não | sim, stubs da IDT | não |
| CPUID / RDMSR / WRMSR | 0 no ELF do kernel | não | host, não o guest | implementados e testados para a identidade virtual |

`sim` na coluna ChrisCPU significa que o opcode executa com a semântica descrita em `chriscpu.md` e que a suíte ou o guest `arith` passa por ele. Opcode ausente dessa coluna é `#UD` ou prefixo ignorado. Isso não bloqueia a fundação. Bloqueia M3 e o desktop, e está listado assim no roadmap.
