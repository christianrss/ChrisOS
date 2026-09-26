# Interface de backend de CPU

O contrato está em `ChrisCpuBackend`:

| Operação | Papel |
| --- | --- |
| `init` | prepara o backend para esta máquina. ChrisHV devolve erro |
| `create_cpu` | aloca o vCPU 0 |
| `reset` | volta o estado arquitetural e limpa halt, exceção e IRQ |
| `run` | executa até `max_steps` ou uma saída |
| `inject_irq` | pendura um vetor. A entrega depende de IF e do atraso do STI |
| `get_state` / `set_state` | copiam `ChrisArchitectureState` |
| `invalidate_tlb` | incrementa uma geração. Não há cache de tradução ainda |
| `shutdown` | libera o que o backend alocou |

As saídas são `HLT`, `SHUTDOWN`, `EXCEPTION`, `TRIPLE`, `BREAK`, `STEP_LIMIT` e `UNMAPPED`.

O estado compartilhado usa um `union` dos 16 GPRs com os nomes `rax`…`r15`. ChrisCPU indexa e também nomeia o mesmo armazenamento. ChrisHV, quando existir, importa e exporta essa struct para o VMCS ou o VMCB. Não há segundo modelo de registradores.

XMM está reservado e zerado. Nenhuma instrução SSE escreve nele. Anunciar SSE no CPUID antes disso está proibido.
