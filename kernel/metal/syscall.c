#include "syscall.h"

#include "gdt.h"
#include "graphics.h"
#include "idt.h"
#include "serial.h"
#include "panic.h"

static int g_user_exited;
static int g_user_exit_code;
static uint64_t g_user_kernel_rip;
static uint64_t g_user_map_lo = 0x400000ull;
static uint64_t g_user_map_hi = 0x401000ull;

void syscall_set_user_map(uint64_t lo, uint64_t hi) {
    g_user_map_lo = lo;
    g_user_map_hi = hi;
}

void syscall_set_kernel_return(uint64_t rip) {
    g_user_kernel_rip = rip;
}

int user_exited(void) {
    return g_user_exited;
}

int user_exit_code(void) {
    return g_user_exit_code;
}

static int copy_from_user(uint64_t uaddr, void *kdst, uint32_t n) {
    uint32_t i;
    const uint8_t *src;

    if (n == 0) {
        return 0;
    }
    if (uaddr < g_user_map_lo || uaddr + (uint64_t)n > g_user_map_hi) {
        return -1;
    }
    src = (const uint8_t *)(uintptr_t)uaddr;
    for (i = 0; i < n; ++i) {
        ((uint8_t *)kdst)[i] = src[i];
    }
    return 0;
}

static void syscall_return_to_kernel(struct irq_frame *frame) {
    frame->rip = g_user_kernel_rip;
    frame->cs = GDT_KERNEL_CODE;
    frame->rflags = 0x202ull;
    g_user_exited = 1;
}

void syscall_dispatch(struct irq_frame *frame) {
    uint64_t nr = frame->rax;

    g_user_exited = 0;

    if (nr == SYS_EXIT) {
        g_user_exit_code = (int)frame->rdi;
        syscall_return_to_kernel(frame);
        return;
    }

    if (nr == SYS_WRITE) {
        uint8_t buf[80];
        uint32_t n = (uint32_t)frame->rdx;

        if (frame->rdi != 1 || n > 80u) {
            frame->rax = (uint64_t)-1;
            frame->rip += 2;
            return;
        }
        if (copy_from_user(frame->rsi, buf, n) != 0) {
            frame->rax = (uint64_t)-1;
            frame->rip += 2;
            return;
        }
        buf[n] = 0;
        serial_puts((const char *)buf);
        frame->rax = n;
        frame->rip += 2;
        return;
    }

    if (nr == SYS_PUTPIXEL) {
        gfx_put_pixel((int)frame->rdi, (int)frame->rsi, (uint32_t)frame->rdx);
        frame->rax = 0;
        frame->rip += 2;
        return;
    }

    frame->rax = (uint64_t)-1;
    frame->rip += 2;
}

void panic_user_fault(struct irq_frame *frame, uint64_t cr2) {
    serial_puts("\nuser fault rip=");
    serial_write_hex(frame->rip);
    serial_puts(" cr2=");
    serial_write_hex(cr2);
    serial_puts(" err=");
    serial_write_hex(frame->error);
    serial_puts("\n");
    panic("user fault");
}

void syscall_init(void) {
    idt_set_user_gate(0x80u);
}
