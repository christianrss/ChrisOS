# Arquitetura do ChrisVM

```text
ChrisOS
  drivers
    hardware virtual
      ChrisVM          máquina, RAM, barramentos, dispositivos, boot
        ChrisCPU       interpretador x86-64
        ChrisHV        VT-x / SVM, ainda não executa
          hardware
```

O QEMU continua no fluxo de desenvolvimento do kernel. Ele não é backend de execução do ChrisVM. KVM, WHPX, HVF, VirtualBox, VMware, Unicorn e Bochs também não são.

## Fronteiras

| Peça | Responsabilidade | Não faz |
| --- | --- | --- |
| `ChrisMachine` | RAM, mapa físico, boot ELF, ciclo de dispositivos, escolha do backend | não decodifica instrução nem conhece VMCS |
| `ChrisCpuBackend` | `init`, `create_cpu`, `reset`, `run`, `inject_irq`, `get_state`, `set_state`, `invalidate_tlb`, `shutdown` | não é um dispositivo |
| `ChrisArchitectureState` | GPRs, RIP, RFLAGS, CRs, segmentos, GDTR/IDTR, MSRs, XMM reservado | um único modelo para CPU e, depois, VMCS/VMCB |
| `ChrisCPU` | fetch, decode, operandos, execução, flags, exceção, interrupção, commit de RIP | não registra dispositivos |
| `ChrisHV` | mesmo contrato de backend | nesta rodada `init` falha e não toca em VMX, SVM nem `/dev/kvm` |
| barramentos | porta I/O e MMIO | não sabem qual backend está rodando |
| frontend | CLI, serial no stdout, debugger textual | não contém semântica de CPU |

`chrisvm --backend=chriscpu` seleciona o interpretador. `--backend=chrishv` cria a máquina e recusa, para o encaixe existir antes da implementação.

## Onde o código mora

A RAM é alocada em `machine/machine.c`. A leitura e a escrita físicas estão em `buses/mmio.c`, porque RAM e MMIO são o mesmo mapa físico. Não há um segundo caminho de memória dentro do executor.

`cpu/hv/vmx/` e `cpu/hv/svm/` só declaram que o módulo não começou. PCI está no mesmo estado em `buses/pci/pci.h`.

## Pipeline do interpretador

```text
FETCH -> DECODE -> RESOLVE -> EXECUTE -> FLAGS -> EXCEPTION -> INTERRUPT -> COMMIT RIP
```

Se a execução escreve RIP, ela marca `rip_dirty` e o laço não soma o tamanho. `HLT` avança o RIP e sai. Desempenho não é meta desta versão: não há JIT, cache de bloco nem TLB.

## O que já executa

Um ELF independente em `0x1000` faz `MOV`, aritmética, desvio, `CALL`/`RET`, acesso à memória, saída serial e `HLT`. Os testes cobrem flags, `#UD`, `#PF`, `#GP`, `#DE`, ELF malformado, porta sem dispositivo, shutdown, MSR desconhecido, MMIO e endereço físico sem dispositivo.

O kernel ChrisOS ainda não entra. O carregador recusa o ELF higher-half em vez de simular `kstart`.
