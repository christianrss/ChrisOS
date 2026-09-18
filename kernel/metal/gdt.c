#include "gdt.h"
#include "panic.h"

struct __attribute__((packed)) tss64 {
    uint32_t reserved0;
    uint64_t rsp0;
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;
    uint64_t ist1;
    uint64_t ist2;
    uint64_t ist3;
    uint64_t ist4;
    uint64_t ist5;
    uint64_t ist6;
    uint64_t ist7;
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iomap_base;
};

struct __attribute__((packed)) gdt_pointer {
    uint16_t limit;
    uint64_t base;
};

extern unsigned char __stack_top[];

static uint64_t gdt[7] __attribute__((aligned(16)));
static struct tss64 tss;

static void zero_bytes(void *address, uint64_t size) {
    uint8_t *bytes = (uint8_t *)address;
    uint64_t index;

    for (index = 0; index < size; ++index) {
        bytes[index] = 0;
    }
}

static void install_tss_descriptor(void) {
    uint64_t base = (uint64_t)&tss;
    uint64_t limit = sizeof(tss) - 1u;

    gdt[5] = (limit & 0xffffu) |
             ((base & 0xffffffu) << 16) |
             ((uint64_t)0x89u << 40) |
             (((limit >> 16) & 0x0fu) << 48) |
             (((base >> 24) & 0xffu) << 56);
    gdt[6] = base >> 32;
}

void gdt_init(void) {
    struct gdt_pointer pointer;

    _Static_assert(sizeof(struct tss64) == 104, "TSS64 deve ter 104 bytes");
    zero_bytes(gdt, sizeof(gdt));
    zero_bytes(&tss, sizeof(tss));

    gdt[1] = 0x00af9a000000ffffull;
    gdt[2] = 0x00cf92000000ffffull;
    gdt[3] = 0x00cff2000000ffffull;
    gdt[4] = 0x00affa000000ffffull;

    tss.rsp0 = (uint64_t)__stack_top;
    tss.iomap_base = sizeof(tss);
    install_tss_descriptor();

    pointer.limit = sizeof(gdt) - 1u;
    pointer.base = (uint64_t)gdt;

    __asm__ volatile (
        "lgdt %0\n"
        "pushq $0x08\n"
        "leaq 1f(%%rip), %%rax\n"
        "pushq %%rax\n"
        "lretq\n"
        "1:\n"
        "movw $0x10, %%ax\n"
        "movw %%ax, %%ds\n"
        "movw %%ax, %%es\n"
        "movw %%ax, %%ss\n"
        "xorw %%ax, %%ax\n"
        "movw %%ax, %%fs\n"
        "movw %%ax, %%gs\n"
        "movw $0x28, %%ax\n"
        "ltr %%ax\n"
        :
        : "m"(pointer)
        : "rax", "memory"
    );

    if (gdt_read_tr() != GDT_TSS) {
        panic("ltr nao carregou o seletor TSS");
    }
}

uint16_t gdt_read_tr(void) {
    uint16_t selector;
    __asm__ volatile ("str %0" : "=r"(selector));
    return selector;
}
