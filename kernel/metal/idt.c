#include <stdint.h>
#include "gdt.h"
#include "idt.h"

struct __attribute__((packed)) idt_gate {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t ist;
    uint8_t attributes;
    uint16_t offset_middle;
    uint32_t offset_high;
    uint32_t reserved;
};

struct __attribute__((packed)) idt_pointer {
    uint16_t limit;
    uint64_t base;
};

extern void (*isr_stub_table[256])(void);
extern void nmi_entry(void);

static struct idt_gate idt[256] __attribute__((aligned(16)));

static void idt_set_gate_attr(unsigned int vector, void (*handler)(void),
                              uint8_t attributes) {
    uint64_t address = (uint64_t)handler;

    idt[vector].offset_low = (uint16_t)address;
    idt[vector].selector = GDT_KERNEL_CODE;
    idt[vector].ist = 0;
    idt[vector].attributes = attributes;
    idt[vector].offset_middle = (uint16_t)(address >> 16);
    idt[vector].offset_high = (uint32_t)(address >> 32);
    idt[vector].reserved = 0;
}

static void idt_set_gate(unsigned int vector, void (*handler)(void)) {
    idt_set_gate_attr(vector, handler, 0x8e);
}

void idt_set_user_gate(unsigned int vector) {
    idt_set_gate_attr(vector, isr_stub_table[vector], 0xee);
}

void idt_load(void) {
    struct idt_pointer pointer;

    pointer.limit = sizeof(idt) - 1u;
    pointer.base = (uint64_t)idt;
    __asm__ volatile ("lidt %0" : : "m"(pointer) : "memory");
}

void idt_init(void) {
    unsigned int vector;

    _Static_assert(sizeof(struct idt_gate) == 16, "gate IDT deve ter 16 bytes");
    for (vector = 0; vector < 256; ++vector) {
        idt_set_gate(vector, isr_stub_table[vector]);
    }
    idt_set_gate(2u, nmi_entry);
    idt_load();
}
