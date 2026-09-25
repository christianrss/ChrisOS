# VMX

Estado: `NOT STARTED`.

Não há leitura de `IA32_FEATURE_CONTROL`, VMXON, VMCS, EPT nem bitmap de I/O. O cabeçalho `cpu/hv/vmx/vmx.h` existe para a fronteira não nascer dentro do interpretador.

A sequência, quando esta rodada de fundação não for mais o limite, é a capacidade do CPUID, os requisitos de CR0 e CR4, VMXON, revisão do VMCS, estado de host e guest, controles, VMLAUNCH, VMRESUME e o tratamento de VM-exit. EPT vem depois, com MMIO de propósito não mapeado como RAM para a violação cair no barramento que o ChrisCPU já usa.
