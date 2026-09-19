#!/usr/bin/env python3
"""Write learn/ book files and prepend Limine adendas. UTF-8, LF."""
from pathlib import Path

ROOT = Path("/mnt/e/Aulas/ChrisOS/learn")

ADENDA_IRQ = """## Atualização Limine 64-bit

Portas iguais. Stubs `iretq`, IDT 16 bytes. EOI só `irq_eoi()`. Path `kernel/`.

"""

ADENDA_KERNEL = """## Atualização Limine 64-bit

Código em `kernel/`. Sem `final.c` / VBE `boot.asm`. FB `g_fb` 32 bpp. FS P15–18 = RAM; disco = F4.4.

"""

ADENDA_F3D = """## Atualização Limine 64-bit

**Não** mude `0x111`→`0x118`. Use `g_fb.w/h` do framebuffer Limine. **Não** mexa em [`host/editor.c`](../../host/editor.c). Código em `kernel/`. Sem VBE em `boot.asm`.

"""

BANNER = "> Feito na era BIOS. Não refazer. Metal atual: [PASSO 01e](../PASSO_01e.md).\n"


def write(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text.replace("\r\n", "\n"), encoding="utf-8", newline="\n")
    print("wrote", path)


def prepend(path: Path, block: str) -> None:
    if not path.exists():
        print("missing", path)
        return
    data = path.read_text(encoding="utf-8", errors="replace")
    if "Atualização Limine 64-bit" in data:
        print("skip", path)
        return
    path.write_text(block + data, encoding="utf-8", newline="\n")
    print("adenda", path)


def fix_historico(name: str) -> None:
    p = ROOT / "historico-bios" / name
    data = p.read_text(encoding="utf-8", errors="replace")
    lines = data.splitlines()
    if lines and lines[0].startswith(">"):
        lines[0] = BANNER.strip()
        data = "\n".join(lines) + "\n"
    else:
        data = BANNER + data
    p.write_text(data, encoding="utf-8", newline="\n")
    print("historico", p)


PASSO_01E = r"""# PASSO 01e — ISO Limine BIOS + UEFI

Tempo: 45 min. Anterior: [historico-bios](historico-bios/PASSO_00.md) · Próximo: [PASSO_01f.md](PASSO_01f.md)

## Objetivo

Gerar `os.iso` híbrido: menu Limine em BIOS (SeaBIOS) e em UEFI (OVMF). Você **não** escreve EFI app — `BOOTX64.EFI` vem do Limine.

## Por que

O MBR em [`boot/boot.asm`](../boot/boot.asm) não escala (`mov al, 128`, VBE `0x8000`, stack `0x90000`). Limine carrega um ELF64 higher-half e entrega framebuffer, HHDM e mmap. P00–P01c ficam em [`historico-bios/`](historico-bios/PASSO_00.md) — não refazer.

## Arquivo

Não tocar: `boot/boot.asm`, `host/`.

```bash
sudo apt-get install -y xorriso mtools nasm gcc ovmf
git clone https://github.com/limine-bootloader/limine.git --branch=v9.x-binary --depth=1 third_party/limine
make -C third_party/limine
mkdir -p iso_root/boot/limine iso_root/EFI/BOOT
cp third_party/limine/limine-bios.sys third_party/limine/limine-bios-cd.bin \
   third_party/limine/limine-uefi-cd.bin iso_root/boot/limine/
cp third_party/limine/BOOTX64.EFI iso_root/EFI/BOOT/
```

`iso_root/boot/limine/limine.conf` (LF, não CRLF):

```
timeout: 3
/ChrisOS
    protocol: limine
    path: boot():/boot/kernel.elf
```

Makefile — **sem** `cat boot.bin`:

```makefile
ISO := os.iso
LIMINE := third_party/limine
iso: $(ISO)
$(ISO): iso_root/boot/limine/limine.conf
	xorriso -as mkisofs -R -r -J \
	  -b boot/limine/limine-bios-cd.bin -no-emul-boot -boot-load-size 4 -boot-info-table \
	  -hfsplus -apm-block-size 2048 \
	  --efi-boot boot/limine/limine-uefi-cd.bin -efi-boot-part --efi-boot-image \
	  --protective-msdos-label iso_root -o $(ISO)
	$(LIMINE)/limine bios-install $(ISO)
```

[`run.bat`](../run.bat): `qemu-system-x86_64.exe -L "C:/qemu19" -boot d -cdrom os.iso -m 256M`

`run-uefi.bat`: `wsl qemu-system-x86_64 -boot d -bios /usr/share/ovmf/OVMF.fd -cdrom os.iso -m 256M`

Magics/markers: use o `limine.h` **dessa pasta** (`v9.x-binary`). Não invente IDs.

## O que não tocar

`boot/boot.asm`, `host/`, `learn/historico-bios/`.

## Como verificar

`make iso`. BIOS e OVMF mostram o menu Limine. Kernel ELF ainda pode faltar (timeout). Se o menu não aparecer: `limine.conf` com CRLF, ou ISO sem `limine bios-install`.

## Próximo

[PASSO_01f.md](PASSO_01f.md)
"""

PASSO_01F = r"""# PASSO 01f — ELF64 + retângulo no framebuffer

Tempo: 60 min. Anterior: [PASSO_01e.md](PASSO_01e.md) · Próximo: [PASSO_01g.md](PASSO_01g.md)

## Objetivo

Substituir VBE `0x111` + `call 0x1000` por um kernel ELF64 higher-half que pinta um retângulo branco.

## Arquivo

`kernel/linker.ld`, `kernel/start.c`. Markers **do seu** `third_party/limine/limine.h`.

O primeiro `PT_LOAD` tem de ser **alinhado a 4K**. `+ SIZEOF_HEADERS` deixa `VirtAddr` em `...0100` e o Limine faz `PANIC: vmm: Misaligned call to map_pages()`.

```
OUTPUT_FORMAT(elf64-x86-64)
OUTPUT_ARCH(i386:x86-64)
ENTRY(kstart)
PHDRS {
    requests PT_LOAD FLAGS(6);
    text     PT_LOAD FLAGS(5);
    data     PT_LOAD FLAGS(6);
}
SECTIONS {
    . = 0xffffffff80000000;
    .limine_requests : ALIGN(4K) {
        KEEP(*(.limine_requests_start))
        KEEP(*(.limine_requests))
        KEEP(*(.limine_requests_end))
    } :requests
    .text : ALIGN(4K) { *(.text .text.*) } :text
    .rodata : ALIGN(4K) { *(.rodata .rodata.*) } :text
    .data : ALIGN(4K) { *(.data .data.*) } :data
    .bss : ALIGN(4K) {
        *(.bss .bss.*) *(COMMON)
        . = ALIGN(16);
        __stack_bottom = .;
        . += 64K;
        __stack_top = .;
    } :data
    /DISCARD/ : { *(.eh_frame*) *(.note*) *(.comment*) }
}
```

`LIMINE_REQUESTS_START_MARKER` no `limine.h` v9 **já é uma definição de variável**, não um initializer:

```c
#include <stdint.h>
#include <limine.h>

__attribute__((used, section(".limine_requests_start")))
static volatile LIMINE_REQUESTS_START_MARKER;

__attribute__((used, section(".limine_requests")))
static volatile LIMINE_BASE_REVISION(3);

__attribute__((used, section(".limine_requests")))
static volatile struct limine_framebuffer_request fb_req = {
    .id = LIMINE_FRAMEBUFFER_REQUEST, .revision = 0
};

__attribute__((used, section(".limine_requests_end")))
static volatile LIMINE_REQUESTS_END_MARKER;

static void hcf(void) { for (;;) __asm__ volatile ("hlt"); }

void kstart(void) {
    if (LIMINE_BASE_REVISION_SUPPORTED == 0) hcf();
    if (fb_req.response == 0 || fb_req.response->framebuffer_count < 1) hcf();
    struct limine_framebuffer *fb = fb_req.response->framebuffers[0];
    if (fb->bpp != 32) hcf();
    uint32_t *p = (uint32_t *)fb->address;
    uint64_t pitch32 = fb->pitch / 4;
    for (uint64_t y = 40; y < 80; y++)
        for (uint64_t x = 40; x < 200; x++)
            p[y * pitch32 + x] = 0x00FFFFFF;
    hcf();
}
```

```
gcc -m64 -ffreestanding -fno-stack-protector -fno-pic -mno-red-zone \
    -mcmodel=kernel -mno-mmx -mno-sse -mno-sse2 \
    -DLIMINE_API_REVISION=3 -I third_party/limine -c kernel/start.c -o kernel/start.o
ld -m elf_x86_64 -nostdlib -static -z max-page-size=0x1000 \
    -T kernel/linker.ld -o iso_root/boot/kernel.elf kernel/start.o
make iso
```

## Como verificar

Retângulo branco. Preto = requests/markers, bpp≠32, ELF fora de `iso_root/boot/`, gcc com PIE, ou PHDR desalinhado.

O `screendump` VGA 720×400 **não** é o LFB VBE (`0xfd000000`). Confirme com o monitor QEMU: `xp /8xw 0xfd000000`.

## Próximo

[PASSO_01g.md](PASSO_01g.md)
"""

PASSO_01G = r"""# PASSO 01g — GDT, TSS, IDT64, PIC atrás de irq_eoi

Tempo: 90 min. Anterior: [PASSO_01f.md](PASSO_01f.md) · Próximo: [PASSO_01h.md](PASSO_01h.md)

Aulas P03/P04/P01d. Sem `iret` a ring 3.

## Objetivo

Interrupts 64-bit com `iretq`. EOI do PIC só em `irq_eoi()`.

## Tabela GDT

| Sel | Uso | DPL |
|-----|-----|-----|
| 0x08 | kernel code L=1 | 0 |
| 0x10 | kernel data | 0 |
| 0x18 | user code L=1 | 3 |
| 0x20 | user data | 3 |
| 0x28 | TSS 16 B | 0 |

TSS.`RSP0` = `__stack_top`. `ltr $0x28`.

`kernel/port.c`:

```c
#include <stdint.h>
uint8_t inb(uint16_t p) { uint8_t v; __asm__ volatile ("inb %1,%0":"=a"(v):"Nd"(p)); return v; }
void outb(uint16_t p, uint8_t v) { __asm__ volatile ("outb %0,%1"::"a"(v),"Nd"(p)); }
```

`kernel/irq.c` — **único** sítio com `outb(0x20)` de EOI:

```c
void irq_remap(void) {
    outb(0x20, 0x11); outb(0xA0, 0x11);
    outb(0x21, 0x20); outb(0xA1, 0x28);
    outb(0x21, 4);    outb(0xA1, 2);
    outb(0x21, 1);    outb(0xA1, 1);
    outb(0x21, 0xF8); outb(0xA1, 0xEF);
}
void irq_eoi(int irq) {
    if (irq >= 8) outb(0xA0, 0x20);
    outb(0x20, 0x20);
}
```

`kernel/pit.c`:

```c
void InitPIT(unsigned int hz) {
    unsigned int div = 1193182 / hz;
    outb(0x43, 0x36);
    outb(0x40, (uint8_t)div);
    outb(0x40, (uint8_t)(div >> 8));
}
```

Stubs NASM (`bits 64`): `push vec; jmp isr_common` → C `irq_dispatch` → `iretq`. Mínimo vec 32 (PIT), 33 (kbd), 44 (mouse). Dispatch: incrementa `ticks` + `irq_eoi(0)`; porta 0x60; PS/2 01d.

`kstart` após o retângulo: `gdt_init(); tss_init(); idt_init(); irq_remap(); InitPIT(60); InitialiseMouse(); sti;` depois `hlt` até `ticks` mudar.

## Como verificar

`ticks` ~60/s. `grep -n "outb.*0x20" kernel/*.c` só em `irq.c` (e o comando ATA `0x20` em F4.4, que não é PIC).

## Próximo

[PASSO_01h.md](PASSO_01h.md)
"""

PASSO_01H = r"""# PASSO 01h — HHDM, mmap, MP off, ata_read −1

Tempo: 45 min. Anterior: [PASSO_01g.md](PASSO_01g.md) · Próximo: [PASSO_gfx.md](PASSO_gfx.md)

**Obrigatório** (ATA e user dependem disto).

## Objetivo

Pedir HHDM + memmap + MP (flags=0, não ligue SMP) + firmware type. `ata_read` ainda devolve −1.

## O que colar

Junto dos requests:

```c
static volatile struct limine_hhdm_request hhdm_req = { .id = LIMINE_HHDM_REQUEST, .revision = 0 };
static volatile struct limine_memmap_request mmap_req = { .id = LIMINE_MEMMAP_REQUEST, .revision = 0 };
static volatile struct limine_mp_request mp_req = { .id = LIMINE_MP_REQUEST, .revision = 0, .flags = 0 };
static volatile struct limine_firmware_type_request fw_req = { .id = LIMINE_FIRMWARE_TYPE_REQUEST, .revision = 0 };

uint64_t hhdm_offset;
void *phys_to_virt(uint64_t p) { return (void *)(p + hhdm_offset); }
int ata_read(uint32_t lba, void *buf, int n) { (void)lba;(void)buf;(void)n; return -1; }
```

`kstart`: `hhdm_offset = hhdm_req.response->offset;` (hcf se response NULL). Não escreva `goto_address`. Não use `-smp 2` ainda.

## Como verificar

Boot igual 01g; `cpu_count >= 1` (do `mp_req.response`).

## Próximo

[PASSO_gfx.md](PASSO_gfx.md)
"""

PASSO_GFX = r"""# PASSO gfx — 32 bpp (P02/P05/P06)

Tempo: 60 min. Anterior: [PASSO_01h.md](PASSO_01h.md) · Próximo: [PASSO_font.md](PASSO_font.md)

## Objetivo

Framebuffer Limine 32 bpp + backbuffer em BSS. Sem `VBEInfoAddress` / `ScreenBufferAddress`.

Copie [`boot/graphics.c`](../boot/graphics.c) → `kernel/graphics.c`.

```c
struct Framebuffer { uint32_t *addr, *back; int w, h, pitch32; };
extern struct Framebuffer g_fb;
#define MAX_W 1920
#define MAX_H 1080
uint32_t backbuffer[MAX_W * MAX_H];

uint32_t rgb32(int r, int g, int b) { return ((uint32_t)r<<16)|((uint32_t)g<<8)|(uint32_t)b; }
uint32_t rgb(int r, int g, int b) { return rgb32(r*255/15, g*255/31, b*255/15); }

void Draw(int x, int y, int r, int g, int b) {
    if (x<0||y<0||x>=g_fb.w||y>=g_fb.h) return;
    g_fb.back[y * g_fb.w + x] = rgb(r,g,b);
}
void Flush(void) {
    for (int y = 0; y < g_fb.h; y++) {
        uint32_t *dst = g_fb.addr + y * g_fb.pitch32;
        uint32_t *src = g_fb.back + y * g_fb.w;
        for (int x = 0; x < g_fb.w; x++) dst[x] = src[x];
    }
}
```

`palette16[]` em 0xRRGGBB. `PutPixel`/`Fill` usam paleta. `ClearScreen` = `rgb32(181,232,255)`. Resolução = `g_fb.w/h` a partir do Limine (clip a MAX_*).

## Como verificar

Ecrã azul-claro; `Fill(0,0,g_fb.w,40,10)` lima.

## Próximo

[PASSO_font.md](PASSO_font.md)
"""

PASSO_FONT = r"""# PASSO font — static const

Tempo: 20 min. Anterior: [PASSO_gfx.md](PASSO_gfx.md) · Próximo: [PASSO_desktop.md](PASSO_desktop.md)

**Antes** do editor. Em `kernel/font.c` (cópia de [`boot/font.c`](../boot/font.c)):

- Os 8 arrays **file-scope** (não locais da função).
- `static const unsigned int characters_arial_N[][150]`.

Se ficarem como automáticos da função (~62 KiB), a stack rebenta mesmo em 64-bit.

## Como verificar

`nm iso_root/boot/kernel.elf | grep characters_arial` em `r`/`d`. `DrawString("OK")` sem freeze.

## Próximo

[PASSO_desktop.md](PASSO_desktop.md)
"""

PASSO_DESKTOP = r"""# PASSO desktop — tasks, FS RAM, rato

Tempo: 90 min. Anterior: [PASSO_font.md](PASSO_font.md) · Próximo: [fase4-metal](fase4-metal/INDEX.md)

## Objetivo

Desktop cooperativo no kernel 64-bit: taskbar, cursor, Welcome. Editor **só** pelo botão (P14), sem auto-spawn.

## O que colar

1. PS/2 + teclado a partir de [`boot/input.c`](../boot/input.c). Clamp `mx < g_fb.w`, `my < g_fb.h`.
2. `task.c`, `graphics_elements.c`, `fs.c`. `#define FS_BACKEND_RAM 1`.
3. `kstart` chama `start()` em vez de `hcf`:

```c
fs_init();
/* ClearScreen, Welcome, Taskbar, DrawMouse — iguais a boot/main.c */
unsigned last = ticks;
for (;;) {
    ProcessTasks();
    Flush();
    while (ticks == last) __asm__ volatile ("hlt");
    last = ticks;
}
```

4. Welcome/taskbar com `Fill`. `mouse_possessed_task_id` = taskbar. **Sem** auto-spawn Editor (P14).
5. Makefile: um `.o` por `.c` + stubs + `-I host` para `editor.c`.

QEMU com disco IDE: use `-boot d` para o CD não perder para `disk.img`.

## Como verificar

Taskbar, cursor, banner; Editor pelo botão não trava. LFB: pixel `(0,0)` ≈ `0x00000080` (botão esquerdo da taskbar).

## Próximo

Adendas P07–P28, depois [F4.1](fase4-metal/PASSO_F4.1.md).
"""

INDEX = r"""# Livro ChrisOS — você cola o código

Cada passo é um conceito. O metal **atual** é Limine + x86-64 em `kernel/`. A era BIOS (MBR + VBE) está arquivada — não refazer.

Regra: este diretório é o livro. Não é o OS. Ver [docs/TEMPLE.md](../docs/TEMPLE.md).

Forma de um passo: [_TEMPLATE.md](_TEMPLATE.md).

## Era BIOS (arquivo)

P00–P01c: [`historico-bios/`](historico-bios/PASSO_00.md). Tag git `era-bootsector`. `make img` ainda gera `os.img` a partir de `boot/`.

| PASSO | Fase | Arquivo |
|------:|------|----------|
| [00](historico-bios/PASSO_00.md) | arquivo | mapa BIOS |
| [01](historico-bios/PASSO_01.md) | arquivo | linker + 128 setores |
| [01b](historico-bios/PASSO_01b.md) | arquivo | tela preta |
| [01c](historico-bios/PASSO_01c.md) | arquivo | BSS `0x12000` |

## Metal Limine 64-bit

| PASSO | Fase | Arquivo |
|------:|------|----------|
| [01e](PASSO_01e.md) | 0 metal | ISO Limine BIOS+UEFI |
| [01f](PASSO_01f.md) | 0 metal | ELF64 + retângulo FB |
| [01g](PASSO_01g.md) | 0 metal | GDT/TSS/IDT64 + `irq_eoi` |
| [01h](PASSO_01h.md) | 0 metal | HHDM, mmap, MP off, `ata_read −1` |
| [gfx](PASSO_gfx.md) | 1 gfx | 32 bpp + backbuffer BSS |
| [font](PASSO_font.md) | 1 gfx | `static const` file-scope |
| [desktop](PASSO_desktop.md) | 1 gfx | tasks / FS RAM / rato |

## Livro 32-bit (números iguais, código em `kernel/`)

Adenda Limine no topo de cada ficheiro. Conceito igual; paths `boot/` → `kernel/`; FB `g_fb`.

| PASSO | Fase | Arquivo |
|------:|------|----------|
| [01d](PASSO_01d.md) | 0 metal | mouse PS/2 |
| [02](PASSO_02.md) | 0 metal | framebuffer |
| [03](PASSO_03.md) | 0 metal | PIC 0x20 |
| [04](PASSO_04.md) | 0 metal | PIT 60 Hz |
| [05](PASSO_05.md) | 1 gfx | `Flush` em bloco |
| [06](PASSO_06.md) | 1 gfx | paleta 16 + `PutPixel` |
| [07](PASSO_07.md) | 1 gfx | tick cooperativo |
| [08](PASSO_08.md) | Editor A | `host/editor.c` buffer |
| [09](PASSO_09.md) | Editor A | caret, insert, backspace, setas |
| [10](PASSO_10.md) | Editor A | `host/test_editor.c` |
| [11](PASSO_11.md) | Editor B | janela `CodeEditorTask` |
| [12](PASSO_12.md) | Editor B | viewport + scroll |
| [13](PASSO_13.md) | Editor B | cursor piscando |
| [14](PASSO_14.md) | Editor B | botão Editor na taskbar |
| [15](PASSO_15.md) | 2 FS | layout RedSea-lite (RAM) |
| [16](PASSO_16.md) | 2 FS | read/write |
| [17](PASSO_17.md) | 2 FS | carregar arquivo como task |
| [18](PASSO_18.md) | 2 FS | Ball fora do monolito |
| [19](PASSO_19.md) | Editor C | Save |
| [20](PASSO_20.md) | Editor C | Open / New |
| [21](PASSO_21.md) | Editor C | dirty `*` no título |
| [22](PASSO_22.md) | 3 CLVM | vendor `clvm_format.h` |
| [23](PASSO_23.md) | 3 CLVM | vendor `clvm_loader.c` |
| [24](PASSO_24.md) | 3 CLVM | interpreter + `SYS` |
| [25](PASSO_25.md) | 3 CLVM | `snake.clvm` no FS |
| [26](PASSO_26.md) | Editor D | Run / F5 |
| [27](PASSO_27.md) | Editor D | erros na status bar |
| [28](PASSO_28.md) | 5 ChrisC | subset + highlight; áudio depois |

CLVM v1 dos labs no portfólio de aulas **não se altera**. O perfil on-OS (memória maior + `SYS`) só existe aqui, a partir do PASSO 22.

## Fases seguintes

| Fase | Pasta | Conteúdo |
|------|-------|----------|
| 4 metal | [fase4-metal/INDEX.md](fase4-metal/INDEX.md) | F4.1 UEFI, F4.2 APIC, F4.3 SMP, F4.4 ATA, F4.5 usermode |
| 3D | [fase3d/INDEX.md](fase3d/INDEX.md) | `g_fb.w/h` (não VBE `0x118`), `gfx.h`, Z-buffer, wireframe |
| GPU *(futuro)* | `learn/fase7-gpu/` | PCI, `gfx_gpu.c` — ver [docs/fase3d/GPU_FUTURO.md](../docs/fase3d/GPU_FUTURO.md) |

Comece em [PASSO_01e.md](PASSO_01e.md) (metal novo) ou [historico-bios/PASSO_00.md](historico-bios/PASSO_00.md) (arquivo).
"""

F4_INDEX = r"""# Fase 4 — metal depois do desktop verde

Pré-requisito: [PASSO desktop](../PASSO_desktop.md) no QEMU (taskbar lima, cursor, Editor pelo botão).

Ordem: F4.1 → F4.2 → F4.3; F4.4 pode paralelo a F4.3; F4.5 por último. Editor permanece ring-0.

| PASSO | Tema | Critério |
|------:|------|----------|
| [F4.1](PASSO_F4.1.md) | UEFI vs BIOS | faixa verde (OVMF) / amarela (BIOS) |
| [F4.2](PASSO_F4.2.md) | APIC timer | `ticks` anda; EOI APIC no timer |
| [F4.3](PASSO_F4.3.md) | SMP `-smp 2` | `cpu1_alive == 1` |
| [F4.4](PASSO_F4.4.md) | ATA PIO | 16 bytes de `disk.img` |
| [F4.5](PASSO_F4.5.md) | usermode hello | pixel verde (100,100) |

QEMU: `-boot d -cdrom os.iso` para o IDE `disk.img` não roubar o boot.
"""

F41 = r"""# F4.1 — UEFI check

Tempo: 20 min. Anterior: [desktop](../PASSO_desktop.md) · Próximo: [F4.2](PASSO_F4.2.md)

## Objetivo

Ver no framebuffer se o firmware é BIOS ou UEFI64.

## O que colar

Pedido `limine_firmware_type_request` (já no 01h). Depois de ter `fb`:

```c
if (fw_req.response) {
    uint32_t c = (fw_req.response->firmware_type == LIMINE_FIRMWARE_TYPE_UEFI64)
        ? 0x0000FF00 : 0x00FFFF00;
    uint32_t *p = (uint32_t *)fb->address;
    for (int i = 0; i < 32; i++) p[i] = c;
}
```

O `Flush` do desktop tapa a faixa; use-a como smoke test **antes** do loop, ou leia `firmware_type` numa string Welcome.

## Como verificar

BIOS = faixa amarela (`0x00FFFF00`); OVMF = verde (`0x0000FF00`).

## Próximo

[F4.2](PASSO_F4.2.md)
"""

F42 = r"""# F4.2 — APIC (PIC timer off)

Tempo: 60 min. Anterior: [F4.1](PASSO_F4.1.md) · Próximo: [F4.3](PASSO_F4.3.md)

Depende 01g+01h. Não ligue SMP antes.

## Objetivo

Timer pelo LAPIC (vec 32). Mascarar IRQ0 do PIC.

O HHDM do Limine **não** cobre MMIO `0xFEE00000` (só RAM). `phys_to_virt(APIC)` dá #PF. Mapeie 4K (ver `kernel/mm.c` `map_mmio`) ou use x2APIC (MSRs).

## O que colar

```c
#define IA32_APIC_BASE 0x1B
static uint32_t *lapic;
static uint32_t lr(uint32_t o) { return lapic[o/4]; }
static void lw(uint32_t o, uint32_t v) { lapic[o/4] = v; }
void apic_eoi(void) { lw(0xB0, 0); }
void apic_init(void) {
    uint32_t lo, hi;
    __asm__ volatile ("rdmsr":"=a"(lo),"=d"(hi):"c"(IA32_APIC_BASE));
    lapic = map_mmio(((uint64_t)hi<<32) | (lo & 0xFFFFF000u));
    lo |= 1u << 11;
    __asm__ volatile ("wrmsr"::"c"(IA32_APIC_BASE),"a"(lo),"d"(hi));
    lw(0xF0, 0x1FF);
    lw(0x3E0, 3);
    lw(0x320, 0x20020); /* periodic, vec 32 */
    lw(0x380, 1000000); /* calibre até ~60 Hz */
    outb(0x21, 0xF9); outb(0xA1, 0xEF); /* mascara IRQ0; teclado+rato no PIC */
}
```

`irq_eoi(0)` → `apic_eoi()`. Vec 0xFF = spurious vazio. Calibre `0x380` contra o PIT do 01g.

Teclado/rato continuam no PIC (IRQ1/12) até haver IOAPIC.

## Como verificar

`ticks` anda; nenhum EOI PIC no **timer**. Desktop e rato vivos.

## Próximo

[F4.3](PASSO_F4.3.md)
"""

F43 = r"""# F4.3 — SMP (`-smp 2`)

Tempo: 40 min. Anterior: [F4.2](PASSO_F4.2.md) · Próximo: [F4.4](PASSO_F4.4.md)

Depende F4.2. Desktop no BSP.

O pedido MP com `flags = 0` faz o Limine acordar APs e deixá-los à espera de `goto_address`. Não use `-smp 2` antes deste passo.

## O que colar

```c
volatile int cpu1_alive;
static void ap_entry(struct limine_mp_info *info) {
    (void)info; cpu1_alive = 1;
    for (;;) __asm__ volatile ("hlt");
}
void smp_start(void) {
    if (!mp_req.response) return;
    for (uint64_t i = 0; i < mp_req.response->cpu_count; i++) {
        struct limine_mp_info *c = mp_req.response->cpus[i];
        if (c->lapic_id == mp_req.response->bsp_lapic_id) continue;
        c->goto_address = ap_entry;
    }
}
```

QEMU: `-smp 2`. Spinlock (`__sync_lock_test_and_set`) só quando o AP tocar em `Flush`.

## Como verificar

`cpu1_alive == 1` (string no Welcome: `ap1`).

## Próximo

[F4.4](PASSO_F4.4.md)
"""

F44 = r"""# F4.4 — ATA PIO

Tempo: 45 min. Anterior: [F4.3](PASSO_F4.3.md) · Próximo: [F4.5](PASSO_F4.5.md)

Disco **à parte** do ISO. P15 **continua RAM**.

## Imagem

```
dd if=/dev/zero of=disk.img bs=1M count=8
printf 'ChrisOS-disk-ok!' | dd of=disk.img conv=notrunc
qemu ... -boot d -cdrom os.iso -drive file=disk.img,format=raw,if=ide,index=0
```

Sem `-boot d` o QEMU pode arrancar o HDD vazio e nunca o ISO.

## O que colar

```c
#define ATA_DATA 0x1F0
#define ATA_SC 0x1F2
#define ATA_L0 0x1F3
#define ATA_L1 0x1F4
#define ATA_L2 0x1F5
#define ATA_DRV 0x1F6
#define ATA_CMD 0x1F7
int ata_read(uint32_t lba, void *buf, int nsects) {
    uint16_t *w = buf;
    unsigned c = 256u * (unsigned)nsects;
    outb(ATA_DRV, 0xE0 | ((lba >> 24) & 0x0F));
    outb(ATA_SC, (uint8_t)nsects);
    outb(ATA_L0, (uint8_t)lba);
    outb(ATA_L1, (uint8_t)(lba >> 8));
    outb(ATA_L2, (uint8_t)(lba >> 16));
    outb(ATA_CMD, 0x20);
    for (int t = 0; t < 1000000; t++) {
        uint8_t s = inb(ATA_CMD);
        if (!(s & 0x80) && (s & 8)) break;
        if (s & 1) return -1;
    }
    __asm__ volatile ("rep insw" : "+D"(w), "+c"(c) : "d"(ATA_DATA) : "memory");
    return nsects;
}
```

Mostre 16 bytes com `DrawString`/`Fill`.

## Como verificar

Bytes = o que está em `disk.img` (`ChrisOS-disk-ok!`). Timeout: bus 0x170 ou conflito com o CD.

## Próximo

[F4.5](PASSO_F4.5.md)
"""

F45 = r"""# F4.5 — usermode hello (último)

Tempo: 90 min. Anterior: [F4.4](PASSO_F4.4.md)

Editor permanece ring-0.

## Objetivo

Pixel verde em (100,100) via `int 0x80` a partir de ring 3. `sys_exit` volta ao loop `start`.

## O que colar

1. `cr3` + `phys_to_virt` → PML4. Páginas scratch alinhadas 4K em BSS.
2. Mapear `0x400000` user+P+W → blob com o programa. Pedido `LIMINE_EXECUTABLE_ADDRESS_REQUEST` para `virt_to_phys` do BSS (KASLR).
3. User (bytes copiados):

```nasm
mov eax, 1
mov edi, 100
mov esi, 100
mov edx, 0x00FF00
int 0x80
mov eax, 0
int 0x80
```

4. IDT 0x80 trap gate **DPL=3**. `sys_putpixel` → backbuffer kernel; `sys_exit` reescreve o frame `iretq` para CS 0x08 e `start`.
5. Entrada:

```c
void enter_user(uint64_t rip, uint64_t rsp) {
    __asm__ volatile (
        "pushq $0x23\n pushq %0\n pushq $0x202\n pushq $0x1B\n pushq %1\n iretq\n"
        :: "r"(rsp), "r"(rip) : "memory");
}
```

RSP user = `0x400000 + 0x400`.

`ClearScreen` tapa o pixel cada frame — volte a pintá-lo no Welcome se quiser vê-lo estável.

## Como verificar

Pixel verde (100,100): no QEMU, `xp /1xw` no LFB (offset `100 * pitch + 100 * 4`) = `0x0000ff00`. #GP se CS/DPL/página user errados; #PF se mapa falhou. `cli` no user → #GP.

## Próximo

[fase3d](../fase3d/INDEX.md) com `g_fb`, sem modo VBE `0x118`.
"""

PASSO_00_NOTE = """> Metal atual: [PASSO 01e](PASSO_01e.md) (Limine 64-bit). Este passo descreve a era BIOS; cópia em [historico-bios/PASSO_00.md](historico-bios/PASSO_00.md).

"""


def main() -> None:
    write(ROOT / "PASSO_01e.md", PASSO_01E)
    write(ROOT / "PASSO_01f.md", PASSO_01F)
    write(ROOT / "PASSO_01g.md", PASSO_01G)
    write(ROOT / "PASSO_01h.md", PASSO_01H)
    write(ROOT / "PASSO_gfx.md", PASSO_GFX)
    write(ROOT / "PASSO_font.md", PASSO_FONT)
    write(ROOT / "PASSO_desktop.md", PASSO_DESKTOP)
    write(ROOT / "INDEX.md", INDEX)
    write(ROOT / "fase4-metal" / "INDEX.md", F4_INDEX)
    write(ROOT / "fase4-metal" / "PASSO_F4.1.md", F41)
    write(ROOT / "fase4-metal" / "PASSO_F4.2.md", F42)
    write(ROOT / "fase4-metal" / "PASSO_F4.3.md", F43)
    write(ROOT / "fase4-metal" / "PASSO_F4.4.md", F44)
    write(ROOT / "fase4-metal" / "PASSO_F4.5.md", F45)

    for n in ("PASSO_00.md", "PASSO_01.md", "PASSO_01b.md", "PASSO_01c.md"):
        fix_historico(n)

    prepend(ROOT / "PASSO_01d.md", ADENDA_IRQ)
    prepend(ROOT / "PASSO_03.md", ADENDA_IRQ)
    prepend(ROOT / "PASSO_04.md", ADENDA_IRQ)
    prepend(ROOT / "PASSO_02.md", ADENDA_KERNEL)
    prepend(ROOT / "PASSO_05.md", ADENDA_KERNEL)
    prepend(ROOT / "PASSO_06.md", ADENDA_KERNEL)
    for i in range(7, 29):
        prepend(ROOT / f"PASSO_{i:02d}.md", ADENDA_KERNEL)
    prepend(ROOT / "fase3d" / "PASSO_01.md", ADENDA_F3D)

    p0 = ROOT / "PASSO_00.md"
    t = p0.read_text(encoding="utf-8", errors="replace")
    if "PASSO 01e" not in t[:400]:
        p0.write_text(PASSO_00_NOTE + t, encoding="utf-8", newline="\n")
        print("banner PASSO_00")


if __name__ == "__main__":
    main()
