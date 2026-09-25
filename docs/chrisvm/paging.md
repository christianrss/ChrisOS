# Paginação

Long mode, quatro níveis:

```text
PML4 -> PDPT -> PD -> PT
```

Páginas de 4 KiB e de 2 MiB estão no caminhante. O bit PS no PD (nível 2) ou no PDPT (nível 1) encerra a caminhada. O boot só usa 2 MiB, com PTE `0x83` (`P|RW|PS`) e físico igual ao virtual, para cada bloco da RAM.

Página de 1 GiB não é formada pelo boot. Um PS no PDPT seria aceito pelo caminhante como página de 1 GiB; ninguém instala essa entrada hoje. Bits reservados dessa forma não são checados.

O PDPT inicial tem uma entrada. Ela cobre o primeiro 1 GiB. Um VA como `0xF0000000` usa `PDPT[3]` e não encontra a tabela, mesmo que alguém escreva um PDE no PD do primeiro gigabyte. MMIO de teste usa um VA ainda no primeiro gigabyte e acima dos 16 MiB de RAM, por exemplo `0x02000000`, e grava o PDE correspondente depois do boot.

Flags honradas: Present, RW, US, Accessed, Dirty, Page Size, NX (se `EFER.NXE`). PWT, PCD e Global são aceitos na entrada e não mudam o comportamento, porque não há cache.

O erro de `#PF` leva o bit de escrita, o bit de usuário e o bit de instrução quando a falha é de execução. O bit P do erro fica 0 quando a entrada está ausente e 1 quando a falha é de permissão.
