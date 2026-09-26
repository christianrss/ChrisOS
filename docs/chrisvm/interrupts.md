# Interrupções

O caminho desenhado é:

```text
dispositivo -> roteador da máquina -> backend->inject_irq
```

O dispositivo não escolhe ChrisCPU ou ChrisHV. O roteador ainda não está ligado a um dispositivo: a serial não gera IRQ. `inject_irq` guarda o vetor. O laço entrega se `IF` está 1. `STI` adia a entrega uma instrução (`sti_delay`).

A entrega usa o mesmo `chris_raise` das exceções. Gate de interrupção (`0xE`) limpa `IF`. Gate de trap (`0xF`) não limpa. Outro tipo de gate falha a entrega. IST diferente de zero falha, porque não há pilha alternativa na TSS.

`INT` e `INT3` empilham o RIP seguinte, o seletor de CS e `RFLAGS`, e no `INT` também o vetor como erro quando a exceção pede código. `IRETQ` desempilha RIP, CS e RFLAGS. A carga de CS exige descritor presente, de código, com L=1.

Com a IDT do boot vazia, `INT` volta ao monitor como `CHRIS_EXIT_EXCEPTION` em vez de triple fault. Isso deixa os testes de `#UD` e `#PF` observáveis. Uma IDT carregada e quebrada segue o caminho `#DF` e depois triple fault, com dump do anel.
