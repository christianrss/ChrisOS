# Roadmap

Estados: `NOT STARTED`, `PARTIAL`, `WORKING`, `VALIDATED`.

`VALIDATED` exige teste que falha se a semântica quebrar. `WORKING` é código que roda no caminho do guest ou da suíte, com buraco conhecido.

| Peça | Estado |
| --- | --- |
| ChrisVM core | WORKING |
| RAM e mapa físico | VALIDATED |
| carregador ELF do protocolo v1 | VALIDATED |
| barramento de portas | VALIDATED |
| barramento MMIO | VALIDATED |
| serial 16550 mínima | PARTIAL |
| framebuffer linear 640×480 | VALIDATED |
| guest de splash no ChrisCPU | VALIDATED |
| janela SDL do framebuffer | WORKING |
| ChrisCPU MOV, ADD, CMP, Jcc, CALL, RET, memória, STOS, HLT | VALIDATED |
| flags da ALU inteira | PARTIAL |
| DIV e IDIV em 8, 16, 32 e 64 | PARTIAL |
| decoder | PARTIAL |
| paginação 4 KiB e 2 MiB | PARTIAL |
| IDT, INT, IRETQ | PARTIAL |
| GDT no boot | WORKING |
| CPUID e MSRs listados | PARTIAL |
| debugger e trace | PARTIAL |
| M0 HLT | VALIDATED |
| M1 guest aritmético | VALIDATED |
| M2 serial do ChrisOS | PARTIAL |
| M3 entry do kernel | NOT STARTED |
| M4 em diante, até o desktop | NOT STARTED |
| PCI, VirtIO | NOT STARTED |
| ChrisHV, VMX, SVM | NOT STARTED |

M2 está parcial porque a serial responde ao loopback `0xAE`, que é o que `serial_init` exige, mas o kernel não chegou a executar essa função. M3 espera o protocolo de boot higher-half, não um atalho de RIP. A tela de inicialização atual é o guest `splash.asm` no framebuffer de `0x02000000`. O ELF do kernel continua fora do protocolo v1.
