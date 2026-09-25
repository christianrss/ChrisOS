# MMU

```text
endereço virtual
    ChrisMMU
endereço físico
    RAM ou MMIO
```

`chris_translate` devolve o físico. `chris_va_read` e `chris_va_write` partem o acesso na fronteira de 4 KiB do físico e chamam o mapa da máquina.

Códigos da tradução:

| Retorno | Significado | Efeito fora da entrega de exceção |
| --- | --- | --- |
| 0 | traduzido | segue |
| -1 | falha de página | `#PF`, `CR2` = endereço |
| -2 | não canônico | `#GP(0)` |
| -3 | a própria entrada de tabela não pôde ser lida | `CHRIS_EXIT_UNMAPPED` |

Canônico exige os bits 63:47 todos iguais. Não canônico não é `#PF`.

Durante `cpu->delivering`, uma falha de leitura ou escrita volta -1 sem aninhar outra exceção. O entregador promove isso a `#DF` e depois a triple fault.

Não há TLB. `invalidate_tlb` só avança um contador. `INVLPG` ainda é `#UD`. Correção primeiro; cache depois.

`CR0.WP` está ligado no boot. Escrita de supervisor em página só de leitura gera `#PF`. Acesso de usuário (`CPL=3`) a página sem `US` também. Bits Accessed e Dirty são escritos de volta na entrada.

NX só vale com `EFER.NXE` e o bit 63 da entrada, em acesso de execução (tipo 2, usado pelo fetch). O boot deixa NXE desligado.
