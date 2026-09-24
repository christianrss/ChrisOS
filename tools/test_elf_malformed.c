#include "elf.h"
#include "pmm.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static uint8_t g_ram[2 * 1024 * 1024];
static int g_budget = 1000;
static int g_used;
static int g_cur;
static int g_proc_used[8];
static int g_proc_n[8];
static uint64_t g_proc_phys[8][64];

uint64_t bootinfo_phys_to_virt(uint64_t phys) {
    return (uint64_t)(uintptr_t)(g_ram + phys);
}

uint64_t pmm_alloc(void) {
    if (g_budget <= 0 || g_used >= 200) {
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

static int g_fail;

static void expect_fail(const char *name, const uint8_t *file, uint32_t n) {
    uint64_t entry = 1;
    int used = g_used;
    int rc = elf_load(file, n, &entry);
    if (rc == 0 || g_used != used || g_cur != 0) {
        fprintf(stderr, "fail: %s rc=%d used %d->%d cur=%d\n",
                name, rc, used, g_used, g_cur);
        g_fail = 1;
    }
}

static void put_u16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void put_u32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static void put_u64(uint8_t *p, uint64_t v) {
    put_u32(p, (uint32_t)v);
    put_u32(p + 4, (uint32_t)(v >> 32));
}

static void base_hdr(uint8_t *f, uint64_t entry, uint64_t phoff, uint16_t phnum) {
    memset(f, 0, 64);
    f[0] = 0x7f;
    f[1] = 'E';
    f[2] = 'L';
    f[3] = 'F';
    f[4] = 2;
    f[5] = 1;
    put_u16(f + 16, 2);
    put_u16(f + 18, 62);
    put_u32(f + 20, 1);
    put_u64(f + 24, entry);
    put_u64(f + 32, phoff);
    put_u16(f + 54, 56);
    put_u16(f + 56, phnum);
}

static void put_ph(uint8_t *ph, uint32_t type, uint32_t flags, uint64_t off,
                   uint64_t vaddr, uint64_t filesz, uint64_t memsz) {
    memset(ph, 0, 56);
    put_u32(ph, type);
    put_u32(ph + 4, flags);
    put_u64(ph + 8, off);
    put_u64(ph + 16, vaddr);
    put_u64(ph + 32, filesz);
    put_u64(ph + 40, memsz);
}

int main(void) {
    uint8_t file[8192];
    uint64_t entry = 0;
    int i;

    memset(file, 0, sizeof(file));
    expect_fail("truncated", file, 10);

    base_hdr(file, 0x401000, 64, 1);
    file[0] = 0;
    expect_fail("magic", file, 128);

    base_hdr(file, 0x401000, UINT64_MAX - 8u, 1);
    expect_fail("phoff", file, 128);

    base_hdr(file, 0x401000, 64, 1);
    put_ph(file + 64, 1, 1, 0x1000, 0x401000, 32, 16);
    expect_fail("filesz>memsz", file, 0x1000);

    base_hdr(file, 0x401000, 64, 1);
    put_ph(file + 64, 1, 1, 0, UINT64_MAX - 16u, 8, 32);
    expect_fail("vaddr overflow", file, 256);

    base_hdr(file, 0x401000, 64, 1);
    put_ph(file + 64, 1, 1, UINT64_MAX - 4u, 0x401000, 16, 16);
    expect_fail("offset overflow", file, 256);

    base_hdr(file, 0x401000, 64, 1);
    put_ph(file + 64, 1, 1, 0x1000, 0x401000, 0, 0);
    expect_fail("zero memsz", file, 0x1100);

    base_hdr(file, 0x401000, 64, 1);
    put_ph(file + 64, 1, 1, 0, 0x1000, 16, 16);
    expect_fail("outside", file, 256);

    base_hdr(file, 0x401000, 64, 2);
    put_ph(file + 64, 1, 1, 0x1000, 0x401000, 16, 16);
    put_ph(file + 120, 1, 1, 0x1000, 0x401000, 16, 16);
    expect_fail("overlap", file, 0x1100);

    base_hdr(file, 0x401000, 64, 1);
    put_ph(file + 64, 2, 1, 0x1000, 0x401000, 16, 16);
    expect_fail("unsupported ph", file, 0x1100);

    base_hdr(file, 0x401000, 64, 1);
    put_ph(file + 64, 1, 3, 0x1000, 0x401000, 16, 16);
    expect_fail("wx", file, 0x1100);

    base_hdr(file, 0x401000, 64, 1);
    put_ph(file + 64, 1, 2, 0x1000, 0x401000, 16, 16);
    expect_fail("entry not exec", file, 0x1100);

    base_hdr(file, 0x401000, 64, 1);
    put_ph(file + 64, 1, 1, 0x1000, 0x401000, 16, 8192);
    file[0x1000] = 0xA5;
    g_budget = 1;
    expect_fail("oom mid", file, 0x1100);
    g_budget = 1000;

    base_hdr(file, 0x401000, 64, 1);
    put_ph(file + 64, 1, 1, 0x1000, 0x401000, 16, 16);
    file[0x1000] = 0xA5;
    if (elf_load(file, 0x1100, &entry) != 0 || entry != 0x401000ull) {
        fprintf(stderr, "fail: success load entry=%llx\n",
                (unsigned long long)entry);
        return 1;
    }
    if (g_used != 1) {
        fprintf(stderr, "fail: success used=%d\n", g_used);
        return 1;
    }
    for (i = 0; i < (int)sizeof(g_ram); ++i) {
        if (g_ram[i] == 0xA5) {
            break;
        }
    }
    if (i == (int)sizeof(g_ram)) {
        fprintf(stderr, "fail: payload missing\n");
        return 1;
    }
    if (g_fail) {
        return 1;
    }
    puts("test_elf_malformed: ok");
    return 0;
}
