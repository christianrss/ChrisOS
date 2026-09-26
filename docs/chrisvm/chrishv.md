# ChrisHV

Estado: interface presente, execução recusada.

`chrishv_backend.init` escreve `chrishv: not implemented (no VMX/SVM in this round)` e falha. Não há ioctl de KVM, VMXON, VMRUN nem mapeamento EPT/NPT.

Quando a máquina e o ChrisCPU estiverem estáveis o bastante para bootar o ChrisOS, o backend privilegiado entra separado do processo:

```text
ChrisVM em userspace
    ioctl próprio
módulo privilegiado
    VMX ou SVM, não os dois ao mesmo tempo
hardware
```

A API comum já é a de `ChrisCpuBackend`: criar vCPU, carregar e salvar `ChrisArchitectureState`, correr, injetar interrupção, invalidar tradução. VMX e SVM implementam isso dos dois lados, sem dispositivo duplicado.

A primeira ISA é a da máquina de desenvolvimento. O código de VMX e o de SVM moram em diretórios separados e ainda não têm instruções.
