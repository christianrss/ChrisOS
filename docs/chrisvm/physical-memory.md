# Memória física

```text
endereço físico
    |
    +-- intervalo contido na RAM --> cópia
    |
    +-- byte coberto por uma região MMIO --> callback do dispositivo
    |
    +-- nenhum dos dois --> CHRIS_EXIT_UNMAPPED
```

`chris_phys_read` e `chris_phys_write` são o único caminho. A RAM do guest não é lida pelo executor com um ponteiro solto, exceto na cópia que essas funções fazem quando o intervalo inteiro está dentro da RAM.

Uma escrita que começa na RAM e atravessa o fim dela não usa o atalho de `memcpy`: cai no laço byte a byte. MMIO é sempre byte a byte nesta versão. O callback recebe tamanho 1. Um acesso de 8 bytes vira oito chamadas. Isso é observável e lento de propósito.

A RAM padrão tem 16 MiB. O tamanho tem de ser múltiplo de 2 MiB e pelo menos 2 MiB, porque o boot publica uma página grande por entrada do PD.

Fora da RAM, o endereço só existe se alguém registrou MMIO ou se uma tradução apontar para um buraco. Buraco traduzido encerra com `CHRIS_EXIT_UNMAPPED` e `CR2` igual ao endereço virtual que falhou. Não há leitura silenciosa de `0xFF` na memória física.
