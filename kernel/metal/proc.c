#include "proc.h"

#include "mm.h"
#include "serial.h"

typedef struct Proc {
    int used;
    int alive;
    uint64_t cr3;
    char name[24];
} Proc;

static Proc g_proc[PROC_MAX];
static int g_current;
static volatile int g_slice;
static ProcFault g_fault;

static void copy_name(char *dst, const char *src) {
    int i = 0;
    if (!src) {
        dst[0] = 0;
        return;
    }
    while (src[i] && i < 23) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = 0;
}

void proc_init(void) {
    int i;
    for (i = 0; i < PROC_MAX; ++i) {
        g_proc[i].used = 0;
        g_proc[i].alive = 0;
        g_proc[i].cr3 = 0;
        g_proc[i].name[0] = 0;
    }
    g_proc[PROC_KERNEL].used = 1;
    g_proc[PROC_KERNEL].alive = 1;
    g_proc[PROC_KERNEL].cr3 = mm_kernel_cr3();
    copy_name(g_proc[PROC_KERNEL].name, "kernel");
    g_current = PROC_KERNEL;
    g_slice = 0;
    g_fault.valid = 0;
}

int proc_create(const char *name) {
    int i;
    uint64_t cr3;
    for (i = 1; i < PROC_MAX; ++i) {
        if (g_proc[i].used) {
            continue;
        }
        cr3 = mm_clone_kernel_space();
        if (cr3 == 0) {
            return -1;
        }
        g_proc[i].used = 1;
        g_proc[i].alive = 1;
        g_proc[i].cr3 = cr3;
        copy_name(g_proc[i].name, name);
        return i;
    }
    return -1;
}

void proc_destroy(int pid) {
    if (pid <= 0 || pid >= PROC_MAX || !g_proc[pid].used) {
        return;
    }
    if (g_current == pid) {
        proc_switch(PROC_KERNEL);
    }
    g_proc[pid].used = 0;
    g_proc[pid].alive = 0;
    g_proc[pid].cr3 = 0;
}

int proc_current(void) {
    return g_current;
}

void proc_switch(int pid) {
    uint64_t cr3;
    if (pid < 0 || pid >= PROC_MAX || !g_proc[pid].used) {
        return;
    }
    cr3 = g_proc[pid].cr3;
    g_current = pid;
    if (cr3 != 0) {
        mm_switch(cr3);
    }
}

uint64_t proc_cr3(int pid) {
    if (pid < 0 || pid >= PROC_MAX || !g_proc[pid].used) {
        return 0;
    }
    return g_proc[pid].cr3;
}

int proc_map_user(int pid, uint64_t virt, uint64_t phys, uint64_t flags) {
    if (pid <= 0 || pid >= PROC_MAX || !g_proc[pid].used) {
        return -1;
    }
    if (virt >= 0x0000800000000000ull) {
        return -1;
    }
    return mm_map_cr3(g_proc[pid].cr3, virt, phys, flags | MM_USER);
}

void proc_on_tick(void) {
    g_slice = 1;
}

int proc_slice_due(void) {
    return g_slice;
}

void proc_slice_ack(void) {
    g_slice = 0;
}

void proc_record_fault(int pid, int tid, uint64_t cr2, uint64_t rip) {
    g_fault.valid = 1;
    g_fault.pid = pid;
    g_fault.tid = tid;
    g_fault.cr2 = cr2;
    g_fault.rip = rip;
    if (pid > 0 && pid < PROC_MAX && g_proc[pid].used) {
        g_proc[pid].alive = 0;
    }
    serial_puts("proc fault pid=");
    serial_write_hex((uint64_t)(uint32_t)pid);
    serial_puts(" rip=");
    serial_write_hex(rip);
    serial_puts(" cr2=");
    serial_write_hex(cr2);
    serial_puts("\n");
}

const ProcFault *proc_last_fault(void) {
    return &g_fault;
}

int proc_alive(int pid) {
    if (pid < 0 || pid >= PROC_MAX || !g_proc[pid].used) {
        return 0;
    }
    return g_proc[pid].alive;
}
