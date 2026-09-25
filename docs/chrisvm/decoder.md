# Decoder

`chris_decode` só transforma bytes em `ChrisInsn`. Quem executa é `chris_execute`. Um opcode desconhecido vira `CHRIS_OP_UD` ou `CHRIS_OP_UNIMPL`; os dois geram `#UD` na execução. Truncar no meio de um imediato ou ModR/M devolve -1, e o laço também gera `#UD`.

O decoder aceita, nesta ordem:

- prefixos `66`, `67`, `F0`, `F2`/`F3` e de segmento;
- um REX final, que vence um REX anterior;
- opcode de um byte ou `0F`;
- ModR/M, SIB, deslocamento e imediato;
- tamanho de operando e de endereço.

`REX.W` força operando de 64 bits. `66` pede 16 bits quando não há `REX.W`. Endereço de 32 bits (`67`) mascara o efetivo com `0xFFFFFFFF`.

RIP-relativo é `mod=0` e `rm=5` sem SIB, em endereço de 64 bits. `REX.B` não transforma esse caso em `R13`. O endereço efetivo é `RIP_da_instrução + tamanho + disp32`, calculado antes do commit do RIP.

O comprimento máximo é 15. Acima disso o decoder marca `#UD` com comprimento 15.

`chris_format_insn` é um mnemônico curto para o trace. Operando de memória aparece como `[mem]`, sem o cálculo do efetivo. O trace também guarda os bytes crus no anel.

O teste de fuzz chama o decoder com 2000 sequências de 1 a 15 bytes, sem criar máquina. O retorno tem de ser -1 ou um comprimento entre 1 e 15 igual a `insn.len`. Isso não prova que o mnemônico está certo; prova que o parser não estoura o buffer.
