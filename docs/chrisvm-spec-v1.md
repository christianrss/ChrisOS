# Chris Virtual Machine Specification v1

Estado: núcleo da máquina e do interpretador em funcionamento. PCI, VirtIO, timer, SMP e ChrisHV não fazem parte desta versão.

A plataforma virtual é uma só. `chrisvm --backend=chriscpu` e, no futuro, `chrisvm --backend=chrishv` devem ver o mesmo mapa, os mesmos dispositivos e o mesmo protocolo de boot. Nenhum código do ChrisOS deve distinguir os backends.

## CPU

Identidade determinística. O CPUID do host não é repassado.

| Folha | Contrato |
| --- | --- |
| 0 | máximo 1. Vendor `ChrisCPU    ` em EBX, EDX, ECX |
| 1 | família/modelo `EAX = 0x00000610`. EDX anuncia TSC, MSR, PSE, APIC, CMOV |
| `0x80000000` | máximo `0x80000001` |
| `0x80000001` | long mode (EDX bit 29). SCE não é anunciado |

Não são anunciados FPU, SSE, AVX, NX nem SYSCALL. Folha desconhecida devolve zero.

MSRs com slot explícito: `IA32_EFER`, `STAR`, `LSTAR`, `CSTAR`, `FMASK`, `FS_BASE`, `GS_BASE`, `KERNEL_GS_BASE`, `IA32_APIC_BASE`. MSR desconhecido gera `#GP`. `SYSCALL` ainda é `#UD`, mesmo que `EFER.SCE` seja escrito.

## Memória

Ver `docs/chrisvm-boot-protocol.md`.

Endereço físico:

1. se o intervalo inteiro cabe na RAM, a cópia é direta;
2. senão, cada byte passa pelo registro de MMIO;
3. endereço sem RAM e sem MMIO é `CHRIS_EXIT_UNMAPPED`.

O PD inicial só cobre a RAM, dentro do primeiro 1 GiB (`PDPT[0]`). Um dispositivo MMIO acima da RAM precisa de uma entrada de página instalada pelo teste ou, no futuro, pela própria máquina. `0xF0000000` não está nesse PDPT: cai no quarto gigabyte.

## I/O e interrupção

O barramento de portas é uma tabela de intervalos. O barramento MMIO é uma tabela de regiões. O executor não contém dispositivos.

O caminho de interrupção previsto é:

```text
dispositivo -> roteador -> backend de CPU
```

Nesta versão o roteador ainda não existe. `inject_irq` está na interface do backend e a entrega espera `RFLAGS.IF`, com atraso de uma instrução depois de `STI`. A IDT vazia do boot não entrega IRQ.

## Fora desta versão

| Item | Estado |
| --- | --- |
| PCI | não iniciado |
| VirtIO block, input, GPU, net, rng, sound | não iniciado |
| timer, APIC, IOAPIC, HPET | não iniciado |
| SMP | não iniciado |
| framebuffer e frontend gráfico | não iniciado; `--headless` é o modo atual |
| ChrisHV / VMX / SVM | recusado na criação da máquina |

O caminho QEMU do ChrisOS permanece o ambiente de boot do kernel. O ChrisVM não o substitui nesta rodada.
