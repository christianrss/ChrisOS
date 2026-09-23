#include <stdint.h>
#include "clvm.h"
#include "clvm_vm.h"

extern uint64_t g_ticks;
extern void rv_trap(void);

static void putc(char c) {
    register long a0 __asm__("a0") = c;
    register long a7 __asm__("a7") = 1;
    __asm__ volatile("ecall" : "+r"(a0) : "r"(a7) : "memory");
}

static void puts(const char *s) {
    while (s && *s)
        putc(*s++);
}

static void putu(uint64_t v) {
    char buf[20];
    int i = 0;
    if (v == 0) {
        putc('0');
        return;
    }
    while (v && i < 20) {
        buf[i++] = (char)('0' + (v % 10));
        v /= 10;
    }
    while (i)
        putc(buf[--i]);
}

static uint64_t rdtime(void) {
    uint64_t t;
    __asm__ volatile("rdtime %0" : "=r"(t));
    return t;
}

static uint64_t g_pt[512] __attribute__((aligned(4096)));

static void sv39(void) {
    uint64_t i;
    uint64_t pt_phys = (uint64_t)(uintptr_t)g_pt;
    for (i = 0; i < 512; ++i)
        g_pt[i] = 0;
    g_pt[0] = (0ull << 10) | 0x0Full;
    g_pt[2] = (0x80000ull << 10) | 0x0Full;
    __asm__ volatile("sfence.vma" ::: "memory");
    __asm__ volatile("csrw satp, %0" ::"r"((8ull << 60) | (pt_phys >> 12)));
    __asm__ volatile("sfence.vma" ::: "memory");
}

static volatile uint32_t *mmio_at(int index) {
    return (volatile uint32_t *)(uintptr_t)(0x10001000ull + (uint64_t)index * 0x1000ull);
}

static uint8_t g_q[2][4096] __attribute__((aligned(4096)));
static uint8_t g_cmd[2][4096] __attribute__((aligned(4096)));

static int vmo_setup(volatile uint32_t *r, int slot) {
    r[0x70 / 4] = 0;
    r[0x70 / 4] = 1;
    r[0x70 / 4] = 3;
    r[0x14 / 4] = 1;
    r[0x20 / 4] = 1;
    r[0x24 / 4] = 1;
    r[0x70 / 4] = 11;
    if ((r[0x70 / 4] & 8u) == 0)
        return 0;
    r[0x30 / 4] = 0;
    r[0x38 / 4] = 4;
    r[0x80 / 4] = (uint32_t)(uintptr_t)g_q[slot];
    r[0x84 / 4] = (uint32_t)(((uintptr_t)g_q[slot]) >> 32);
    r[0x90 / 4] = (uint32_t)(uintptr_t)(g_q[slot] + 64);
    r[0x94 / 4] = (uint32_t)(((uintptr_t)g_q[slot]) >> 32);
    r[0xA0 / 4] = (uint32_t)(uintptr_t)(g_q[slot] + 2048);
    r[0xA4 / 4] = (uint32_t)(((uintptr_t)g_q[slot]) >> 32);
    r[0x44 / 4] = 1;
    r[0x70 / 4] = 15;
    return 1;
}

static void probe_virtio(void) {
    int i;
    int saw_blk = 0;
    int saw_gpu = 0;
    for (i = 0; i < 8; ++i) {
        volatile uint32_t *r = mmio_at(i);
        uint32_t magic = r[0];
        uint32_t devid;
        if (magic != 0x74726976u)
            continue;
        devid = r[2];
        if (devid == 2 && !saw_blk) {
            uint32_t cap;
            if (!vmo_setup(r, 0))
                continue;
            cap = r[0x100 / 4];
            puts("virtio-blk ");
            putu(cap);
            puts("\n");
            saw_blk = 1;
        }
        if (devid == 16 && !saw_gpu) {
            uint32_t *c = (uint32_t *)g_cmd[1];
            uint32_t clo;
            uint32_t chi;
            int n;
            if (!vmo_setup(r, 1))
                continue;
            for (n = 0; n < 16; ++n)
                c[n] = 0;
            c[0] = 0x0101u;
            c[6] = 1;
            c[7] = 1;
            c[8] = 32;
            c[9] = 32;
            clo = (uint32_t)(uintptr_t)c;
            chi = (uint32_t)(((uintptr_t)c) >> 32);
            g_q[1][0] = (uint8_t)clo;
            g_q[1][1] = (uint8_t)(clo >> 8);
            g_q[1][2] = (uint8_t)(clo >> 16);
            g_q[1][3] = (uint8_t)(clo >> 24);
            g_q[1][4] = (uint8_t)chi;
            g_q[1][5] = (uint8_t)(chi >> 8);
            g_q[1][6] = (uint8_t)(chi >> 16);
            g_q[1][7] = (uint8_t)(chi >> 24);
            g_q[1][8] = 40;
            g_q[1][12] = 1;
            g_q[1][13] = 0;
            g_q[1][14] = 1;
            g_q[1][66] = 1;
            r[0x50 / 4] = 0;
            puts("virtio-gpu scanout\n");
            saw_gpu = 1;
        }
    }
    if (!saw_blk)
        puts("virtio-blk miss\n");
    if (!saw_gpu)
        puts("virtio-gpu miss\n");
}

static uint32_t fnv(const uint8_t *p, int n) {
    uint32_t h = 2166136261u;
    int i;
    for (i = 0; i < n; ++i) {
        h ^= p[i];
        h *= 16777619u;
    }
    return h;
}

static void run_vm(void) {
    uint8_t file[32];
    uint8_t code[8];
    ClvmImage image;
    ClvmVm vm;
    ClvmStepResult r;
    int i;
    code[0] = 1;
    code[1] = 1;
    code[2] = 0;
    code[3] = 0;
    code[4] = 0;
    code[5] = 8;
    for (i = 0; i < 32; ++i)
        file[i] = 0;
    file[0] = 'C';
    file[1] = 'L';
    file[2] = 'V';
    file[3] = 'M';
    file[4] = 1;
    file[5] = 1;
    file[8] = 6;
    for (i = 0; i < 6; ++i)
        file[16 + i] = code[i];
    {
        uint32_t c = fnv(code, 6);
        file[12] = (uint8_t)c;
        file[13] = (uint8_t)(c >> 8);
        file[14] = (uint8_t)(c >> 16);
        file[15] = (uint8_t)(c >> 24);
    }
    if (clvm_parse(file, 22, &image) != CL_LOAD_OK) {
        puts("clvm parse fail\n");
        return;
    }
    clvm_vm_init(&vm, &image, 0, 0);
    r = clvm_step(&vm, 1000);
    if (r == CLVM_STEP_HALT)
        puts("clvm halt\n");
    else
        puts("clvm run fail\n");
}

void kmain(void) {
    uint64_t now;
    puts("riscv kernel\n");
    now = rdtime();
    puts("time ");
    putu(now);
    puts("\n");
    sv39();
    puts("sv39 on\n");
    probe_virtio();
    run_vm();
    __asm__ volatile("csrw stvec, %0" ::"r"((uintptr_t)rv_trap));
    {
        unsigned long when = now + 100000ull;
        __asm__ volatile("csrw 0x14d, %0" ::"r"(when));
    }
    {
        unsigned long sie = 0x20;
        __asm__ volatile("csrs sie, %0" ::"r"(sie));
    }
    __asm__ volatile("csrsi sstatus, 2");
    {
        int spins = 0;
        while (g_ticks == 0 && spins < 1000000)
            spins += 1;
    }
    puts("irq ");
    putu(g_ticks);
    puts("\n");
}
