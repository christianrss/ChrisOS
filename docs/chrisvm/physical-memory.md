# Memória física

```text
endereço físico
    |
    +-- intervalo contido na RAM --> cópia
    |
    +-- intervalo contido no framebuffer --> cópia, escrita marca sujo
    |
    +-- byte coberto por uma região MMIO --> callback do dispositivo
    |
    +-- nenhum dos três --> CHRIS_EXIT_UNMAPPED
```

`chris_phys_read` e `chris_phys_write` são o único caminho. A RAM do guest não é lida pelo executor com um ponteiro solto, exceto na cópia que essas funções fazem quando o intervalo inteiro está dentro da RAM. O framebuffer em `0x02000000` segue a mesma regra: o intervalo tem de caber inteiro no retângulo de 1 228 800 bytes. Um acesso que atravessa a borda cai no laço de MMIO.

Uma escrita que começa na RAM e atravessa o fim dela não usa o atalho de `memcpy`: cai no laço byte a byte. MMIO é sempre byte a byte nesta versão. O callback recebe tamanho 1. Um acesso de 8 bytes vira oito chamadas. Isso é observável e lento de propósito. O framebuffer não usa esse laço, porque um `REP STOSD` de 640×480 pixels passaria por mais de um milhão de callbacks.

A RAM padrão tem 16 MiB. O tamanho tem de ser múltiplo de 2 MiB e pelo menos 2 MiB, porque o boot publica uma página grande por entrada do PD.

Fora da RAM, o endereço só existe se alguém registrou MMIO ou se uma tradução apontar para um buraco. Buraco traduzido encerra com `CHRIS_EXIT_UNMAPPED` e `CR2` igual ao endereço virtual que falhou. Não há leitura silenciosa de `0xFF` na memória física.
