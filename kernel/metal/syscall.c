#include "syscall.h"

#include "gdt.h"
#include "graphics.h"
#include "idt.h"
#include "serial.h"
#include "fs.h"
#include "input.h"
#include "proc.h"

static int g_user_exited;
static int g_user_exit_code;
static uint64_t g_user_kernel_rip;
static uint64_t g_user_map_lo = 0x400000ull;
static uint64_t g_user_map_hi = 0x500000ull;

#define UFILE_MAX 8
#define UPATH_MAX 128

typedef struct UFile {
    int used;
    char path[UPATH_MAX];
} UFile;

static UFile g_ufile[UFILE_MAX];

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

static int copy_to_user(uint64_t uaddr, const void *ksrc, uint32_t n) {
    uint32_t i;
    uint8_t *dst;
    if (n == 0)
        return 0;
    if (uaddr < g_user_map_lo || uaddr + (uint64_t)n > g_user_map_hi)
        return -1;
    dst = (uint8_t *)(uintptr_t)uaddr;
    for (i = 0; i < n; ++i)
        dst[i] = ((const uint8_t *)ksrc)[i];
    return 0;
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
        /* +1 so the NUL terminator at buf[n] is in bounds for the maximum
         * accepted length (n == 80); previously buf[80] overflowed the stack. */
        uint8_t buf[81];
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

    if (nr == SYS_FOPEN) {
        char path[UPATH_MAX];
        int fd;
        uint32_t n = 0;
        if (copy_from_user(frame->rdi, path, UPATH_MAX - 1) != 0) {
            frame->rax = (uint64_t)-1;
            frame->rip += 2;
            return;
        }
        path[UPATH_MAX - 1] = 0;
        while (path[n] && n + 1 < UPATH_MAX)
            n++;
        for (fd = 2; fd < UFILE_MAX; ++fd) {
            if (!g_ufile[fd].used) {
                int k;
                g_ufile[fd].used = 1;
                for (k = 0; k < UPATH_MAX; ++k)
                    g_ufile[fd].path[k] = path[k];
                frame->rax = (uint64_t)fd;
                frame->rip += 2;
                return;
            }
        }
        frame->rax = (uint64_t)-1;
        frame->rip += 2;
        return;
    }

    if (nr == SYS_FREAD) {
        int fd = (int)frame->rdi;
        uint32_t n = (uint32_t)frame->rdx;
        char kbuf[512];
        int got;
        if (fd < 2 || fd >= UFILE_MAX || !g_ufile[fd].used || n > 512u) {
            frame->rax = (uint64_t)-1;
            frame->rip += 2;
            return;
        }
        got = fs_read(g_ufile[fd].path, kbuf, (int)n);
        if (got < 0) {
            frame->rax = (uint64_t)-1;
            frame->rip += 2;
            return;
        }
        if (copy_to_user(frame->rsi, kbuf, (uint32_t)got) != 0) {
            frame->rax = (uint64_t)-1;
            frame->rip += 2;
            return;
        }
        frame->rax = (uint64_t)(uint32_t)got;
        frame->rip += 2;
        return;
    }

    if (nr == SYS_FWRITE) {
        int fd = (int)frame->rdi;
        uint32_t n = (uint32_t)frame->rdx;
        char kbuf[512];
        if (n > 512u || copy_from_user(frame->rsi, kbuf, n) != 0) {
            frame->rax = (uint64_t)-1;
            frame->rip += 2;
            return;
        }
        if (fd == 1) {
            kbuf[n < 512u ? n : 511u] = 0;
            serial_puts(kbuf);
            frame->rax = n;
            frame->rip += 2;
            return;
        }
        if (fd < 2 || fd >= UFILE_MAX || !g_ufile[fd].used) {
            frame->rax = (uint64_t)-1;
            frame->rip += 2;
            return;
        }
        frame->rax = (uint64_t)(int)fs_write(g_ufile[fd].path, kbuf, (int)n);
        frame->rip += 2;
        return;
    }

    if (nr == SYS_FCLOSE) {
        int fd = (int)frame->rdi;
        if (fd >= 2 && fd < UFILE_MAX)
            g_ufile[fd].used = 0;
        frame->rax = 0;
        frame->rip += 2;
        return;
    }

    if (nr == SYS_KEY) {
        frame->rax = input_key_down((int)frame->rdi) ? 1ull : 0ull;
        frame->rip += 2;
        return;
    }

    frame->rax = (uint64_t)-1;
    frame->rip += 2;
}

void panic_user_fault(struct irq_frame *frame, uint64_t cr2) {
    int pid = proc_current();
    proc_record_fault(pid, 0, cr2, frame->rip);
    if (pid > 0) {
        proc_destroy(pid);
    }
    serial_puts("\nuser fault rip=");
    serial_write_hex(frame->rip);
    serial_puts(" cr2=");
    serial_write_hex(cr2);
    serial_puts(" err=");
    serial_write_hex(frame->error);
    serial_puts(" (process ended)\n");
    g_user_exit_code = -11;
    syscall_return_to_kernel(frame);
}

void syscall_init(void) {
    idt_set_user_gate(0x80u);
}
