# ChrisCPU

Interpretador de long mode. O guest do protocolo v1 já entra com paginação ligada. Código com `L=0` no descritor de CS gera `#GP` na carga do segmento. Modo real não existe. Se `CR0.PG` for limpo, a tradução cai para identidade; isso só serve para um guest que desligue a paginação, e não é o boot.

## Laço

Cada passo busca até 15 bytes, decodifica, formata uma linha de trace, executa e então:

- se a instrução não sujou o RIP e não parou a CPU, soma `insn.len` ao RIP;
- entrega IRQ pendente se `IF` está ligado e o atraso do `STI` já passou;
- incrementa `steps` e `TSC`.

`TSC` sobe uma unidade por instrução. Não é tempo de parede. `RDTSC` do kernel ainda não está implementado.

## Instruções com semântica executada

`MOV`, `MOVZX`, `MOVSX`, `LEA`, `XCHG`, `PUSH`, `POP`, `PUSHF`, `POPF`, ALU (`ADD`, `ADC`, `SUB`, `SBB`, `AND`, `OR`, `XOR`, `CMP`, `TEST`), `INC`, `DEC`, `NOT`, `NEG`, shifts (`SHL`, `SHR`, `SAR`, `ROL`, `ROR`), `MUL`, `IMUL`, `DIV`, `IDIV`, `JMP`, `Jcc`, `CALL`, `RET`, `LEAVE`, `NOP`, `HLT`, `CLC`, `STC`, `CLD`, `STD`, `CLI`, `STI`, `IN`, `OUT`, `INT`, `INT3`, `IRETQ`, `LGDT`, `LIDT`, `SGDT`, `SIDT`, `MOV CRx`, `CPUID`, `RDMSR`, `WRMSR`, `SETcc`, `CMOVcc`.

`LOCK` em registrador é `#UD`. `LOCK` em memória executa a operação uma vez. Com uma CPU só isso é atômico em relação ao guest; a semântica de SMP ainda não existe.

## Flags

`CF`, `PF`, `AF`, `ZF`, `SF`, `OF` são calculadas para a ALU. `PF` é a paridade par dos 8 bits baixos. O bit 1 de `RFLAGS` permanece 1. `INC` e `DEC` preservam `CF`. Shift com contagem 0 não mexe nas flags; `OF` só é definido para contagem 1. Lógicas zeram `CF` e `OF` e limpam `AF`. Nas lógicas, `AF` é indefinido no x86; aqui ele fica 0 de forma explícita.

`DIV` e `IDIV` usam o dividendo largo (`AX`, `DX:AX`, `EDX:EAX`, `RDX:RAX`). Quociente que não cabe gera `#DE`. O resto tem o sinal do dividendo. `MUL` e `IMUL` ligam `CF` e `OF` juntos quando a metade alta não é extensão do resultado. As outras flags desses produtos ficam como estavam: no x86 elas são indefinidas.

## Limites conhecidos

- o fetch lê 15 bytes mesmo quando a instrução é curta. Um opcode no fim de uma página presente, seguido de página ausente, gera `#PF` antes da execução;
- `RCL` e `RCR` são `#UD`;
- `SYSCALL`, `SYSRET`, `SWAPGS`, `INVLPG`, `FXSAVE`, `FXRSTOR`, SSE, x87, `RDTSC`, `RDRAND`, `PAUSE`, `MFENCE` e `IRET` de 32 bits não estão implementados;
- não há IST. Um gate com IST diferente de zero falha a entrega;
- `#SS` ainda não é separado de `#PF` num push para página ausente.
