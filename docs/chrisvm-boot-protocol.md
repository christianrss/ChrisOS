# ChrisVM boot protocol v1

Versão: `CHRIS_BOOT_PROTOCOL` = 1.

Este contrato é o que o ChrisVM instala antes de `ChrisCPU.run()`. O kernel guest não recebe atalhos escondidos dentro do executor. Um ELF que não cabe neste contrato é recusado com erro explícito.

## O que esta versão faz

O guest já começa em long mode de 64 bits:

- paginação de 4 níveis ligada;
- páginas de 2 MiB com identidade física para toda a RAM;
- CS = 0x08 (código 64 bits, L=1);
- SS = DS = ES = 0x10 (dados);
- CPL = 0;
- IF = 0;
- IDT vazia (base 0, limite 0).

Não há BIOS, MBR, UEFI, Limine interno nem modo real.

## Mapa físico

RAM padrão: 16 MiB, múltiplo de 2 MiB, mínimo 2 MiB.

| Região | Endereço | Conteúdo |
| --- | --- | --- |
| página zero | `0x00000000`–`0x00000FFF` | reservada, zerada |
| imagem | tipicamente `0x1000` | segmentos `PT_LOAD` do ELF |
| GDT | `0x70000` | 3 descritores de 8 bytes |
| pilha | `RSP = 0x80000` | cresce para baixo; a página `0x7F000`–`0x7FFFF` é reservada |
| PML4 | `ram_size - 0x4000` | uma entrada, aponta o PDPT |
| PDPT | `ram_size - 0x3000` | uma entrada, aponta o PD (primeiro 1 GiB) |
| PD | `ram_size - 0x2000` | uma entrada `P|RW|PS` por cada 2 MiB de RAM, mais a página do framebuffer |
| framebuffer | `0x02000000` | 640×480, XRGB8888, pitch 2560, 1 228 800 bytes |

A reserva das tabelas é `CHRIS_PT_RESERVE` = `0x4000`. O carregador recusa um segmento que sobreponha as tabelas, a página da GDT ou a página da pilha.

A RAM padrão de 16 MiB usa os índices 0–7 do PD. O framebuffer fica no índice 16 (`0x02000000 >> 21`). O boot grava `endereço | 0x83` nessa entrada se ela ainda estiver vazia. `chris_machine_create` recusa uma RAM maior que `0x02000000`, para o endereço fixo permanecer fora da RAM. `0x01000000` continua sem página, e é o endereço do teste de `#PF`.

## GDT inicial

| Seletor | Valor | Papel |
| --- | --- | --- |
| 0x00 | 0 | nulo |
| 0x08 | `0x00af9a000000ffff` | código 64 bits, L=1, o mesmo formato do kernel |
| 0x10 | `0x00cf92000000ffff` | dados |

`GDTR.base = 0x70000`, `GDTR.limit = 23`.

## Registradores no entry

| Campo | Valor |
| --- | --- |
| RIP | `e_entry` do ELF, ou o entry passado a `chris_boot` |
| RSP | `0x80000` |
| RFLAGS | 2 |
| CR0 | PE, NE, WP, PG |
| CR4 | PAE |
| CR3 | base do PML4 |
| EFER | LME e LMA. SCE e NXE ficam desligados |

Os demais GPRs começam em zero. Não há estrutura de boot info nesta versão. O kernel do ChrisOS, ligado em `0xffffffff80000000`, é recusado: `higher-half ELF is outside boot protocol v1`. Isso é o milestone M3, não um caso especial de `kstart`.

## ELF aceito

- ELF64, little-endian, `EM_X86_64` (62);
- apenas `PT_LOAD`;
- `p_vaddr` abaixo de `0xffff800000000000` e dentro da RAM;
- `p_memsz` maior que `p_filesz` é zerado (BSS).

## Dispositivos do protocolo

| Dispositivo | Onde | Contrato |
| --- | --- | --- |
| serial 16550 mínimo | portas `0x3F8`–`0x3FF` | TX vai para o buffer e para o hook do frontend. Loopback (MCR bit 4) devolve o byte escrito, para o teste `0xAE` do `serial_init` |
| shutdown de debug | porta `0x501` | escrita de `0x01` termina com `CHRIS_EXIT_SHUTDOWN`. Não é porta de PC nem a `0x604` do QEMU |
| framebuffer linear | físico `0x02000000` | 640×480, um pixel little-endian `0x00RRGGBB`, pitch 2560. Escrita inteira dentro do retângulo copia para o buffer do dispositivo e marca a tela suja. Não é callback de MMIO byte a byte |

Porta de I/O sem dispositivo: `IN` devolve `0xFF` / `0xFFFF` / `0xFFFFFFFF`. `OUT` é ignorado.

## Exceções com IDT vazia

Uma exceção com IDT descarregada volta ao monitor (`CHRIS_EXIT_EXCEPTION`) e não vira triple fault. IDT carregada que falha na entrega promove para `#DF` e, na segunda falha, para triple fault com dump do anel de trace.

## Próxima versão

O protocolo 2 precisa carregar um ELF higher-half, publicar um boot info explícito e manter o mesmo estado arquitetural. O kernel não deve detectar se o backend é ChrisCPU ou ChrisHV.

O guest `guests/splash.asm` usa este framebuffer. Ele pinta o fundo `0x00101828` — a mesma primeira cor de `gfx_clear` em `kstart` — desenha o painel, o título, a barra e o rodapé com `REP STOS`, escreve `splash` na serial e executa `HLT`. Esse quadro é do ChrisCPU. O ELF do kernel continua recusado.
