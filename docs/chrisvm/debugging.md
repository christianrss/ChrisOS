# Depuração e trace

```text
chrisvm --trace guest.elf
chrisvm --debug guest.elf
chrisvm --break=0x1010 guest.elf
```

`--trace` escreve no stderr, via o hook de log:

```text
CPU0 #18 RIP 0000000000001031 CALL
```

O formato é curto: passo, RIP e mnemônico. Bytes completos e o texto ficam no anel circular de 256 entradas. No triple fault o anel é despejado.

`--debug` lê stdin:

| Comando | Efeito |
| --- | --- |
| `s` | uma instrução e dump |
| `c` | segue, limpando o breakpoint atual para não parar no mesmo RIP |
| `r` | dump de RIP, RSP, RFLAGS, RAX–RDI e CR0–CR4 |
| `b HEX` | breakpoint nesse RIP |
| `q` | sai |

Um passo isolado termina com `CHRIS_EXIT_STEP_LIMIT`. Isso é o motivo de o `run` ter parado, não uma falha do guest. `q` antes do `HLT` faz o processo sair com código diferente de zero.

Filtros `--trace-symbol`, `--trace-from` e watchpoints de memória ainda não existem. O anel e o hook são o lugar onde eles entram sem mudar o executor.

`--trace-memory`, `--trace-io` e `--trace-mmio` são aceitos. I/O emite uma linha `io out` quando o flag está ligado. Memória e MMIO ainda não imprimem cada acesso; o flag fica guardado para não mudar a linha de comando quando a impressão chegar.
