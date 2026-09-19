#ifndef CHRISOS_SYSCALL_H
#define CHRISOS_SYSCALL_H

#include <stdint.h>
#include "irq.h"

#define SYS_EXIT     1u
#define SYS_WRITE    2u
#define SYS_PUTPIXEL 3u

void syscall_init(void);
void syscall_dispatch(struct irq_frame *frame);
void syscall_set_kernel_return(uint64_t rip);
void syscall_set_user_map(uint64_t lo, uint64_t hi);
void panic_user_fault(struct irq_frame *frame, uint64_t cr2);

int user_exited(void);
int user_exit_code(void);

#endif
