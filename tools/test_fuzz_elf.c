#include "elf.h"
#include "pmm.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static uint8_t g_ram[2 * 1024 * 1024];
static int g_budget = 8;
static int g_used;
static int g_cur;
static int g_proc_used[8];
static int g_proc_n[8];
static uint64_t g_proc_phys[8][64];
static uint32_t g_state = 1u;

uint64_t bootinfo_phys_to_virt(uint64_t phys) {
    if (phys >= sizeof(g_ram)) {
        return (uint64_t)(uintptr_t)g_ram;
    }
    return (uint64_t)(uintptr_t)(g_ram + phys);
}

uint64_t pmm_alloc(void) {
    if (g_budget <= 0 || g_used >= 64) {
        return 0;
    }
    g_budget--;
    g_used++;
    return (uint64_t)g_used * PMM_PAGE;
}

void pmm_free(uint64_t phys) {
    if (phys == 0) {
        return;
    }
    g_used--;
    g_budget++;
}

int proc_current(void) {
    return g_cur;
}

void proc_switch(int pid) {
    g_cur = pid;
}

int proc_create(const char *name) {
    int i;
    (void)name;
    for (i = 1; i < 8; ++i) {
        if (!g_proc_used[i]) {
            g_proc_used[i] = 1;
            g_proc_n[i] = 0;
            return i;
        }
    }
    return -1;
}

void proc_destroy(int pid) {
    int i;
    if (pid <= 0 || pid >= 8 || !g_proc_used[pid]) {
        return;
    }
    for (i = 0; i < g_proc_n[pid]; ++i) {
        pmm_free(g_proc_phys[pid][i]);
    }
    g_proc_n[pid] = 0;
    g_proc_used[pid] = 0;
    if (g_cur == pid) {
        g_cur = 0;
    }
}

int proc_map_owned(int pid, uint64_t virt, uint64_t phys, uint64_t flags) {
    (void)virt;
    (void)flags;
    if (pid <= 0 || pid >= 8 || !g_proc_used[pid] || g_proc_n[pid] >= 64) {
        return -1;
    }
    g_proc_phys[pid][g_proc_n[pid]++] = phys;
    return 0;
}

void syscall_set_user_map(uint64_t lo, uint64_t hi) {
    (void)lo;
    (void)hi;
}

static uint32_t rnd(void) {
    g_state = g_state * 1664525u + 1013904223u;
    return g_state;
}

int main(void) {
    int i;
    for (i = 0; i < 300; ++i) {
        uint8_t file[256];
        uint64_t entry = 0;
        uint32_t n = 1u + (rnd() % 256u);
        uint32_t k;
        int used;
        int rc;
        for (k = 0; k < n; ++k) {
            file[k] = (uint8_t)rnd();
        }
        if ((rnd() & 7u) == 0) {
            file[0] = 0x7f;
            file[1] = 'E';
            file[2] = 'L';
            file[3] = 'F';
        }
        used = g_used;
        g_budget = 4;
        rc = elf_load(file, n, &entry);
        if (rc != 0 && rc != -1) {
            fprintf(stderr, "fail: rc %d\n", rc);
            return 1;
        }
        if (rc != 0 && (g_used != used || g_cur != 0)) {
            fprintf(stderr, "fail: leak used %d->%d cur %d\n", used, g_used, g_cur);
            return 1;
        }
        if (rc == 0) {
            proc_destroy(g_cur);
            g_cur = 0;
        }
    }
    puts("test_fuzz_elf: ok");
    return 0;
}
