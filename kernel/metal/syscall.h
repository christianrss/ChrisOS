#ifndef CHRISOS_SYSCALL_H
#define CHRISOS_SYSCALL_H

#include <stdint.h>
#include "irq.h"

#define SYS_EXIT     1u
#define SYS_WRITE    2u
#define SYS_PUTPIXEL 3u
#define SYS_FOPEN    4u
#define SYS_FREAD    5u
#define SYS_FWRITE   6u
#define SYS_FCLOSE   7u
#define SYS_KEY      8u

void syscall_init(void);
void syscall_dispatch(struct irq_frame *frame);
void syscall_set_kernel_return(uint64_t rip);
void syscall_set_user_map(uint64_t lo, uint64_t hi);
void panic_user_fault(struct irq_frame *frame, uint64_t cr2);

int user_exited(void);
int user_exit_code(void);
void syscall_close_owner(int pid);

/* Place the NUL for SYS_WRITE. n == 80 needs cap >= 81. */
static inline int syscall_write_term(uint8_t *dst, int cap, uint32_t n) {
    if (!dst || n > 80u || cap < (int)n + 1) {
        return -1;
    }
    dst[n] = 0;
    return 0;
}

#endif
