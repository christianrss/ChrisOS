#ifndef CHRISOS_IDT_H
#define CHRISOS_IDT_H

void idt_init(void);
void idt_load(void);
void idt_set_user_gate(unsigned int vector);

#endif
