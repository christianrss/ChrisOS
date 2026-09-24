#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bootinfo.h"
#include "heap.h"
#include "pmm.h"

#define NCPU 4
#define PAGES_PER 400

extern void host_set_cpu(uint32_t id);

static uint64_t g_pages[NCPU][PAGES_PER];
static int g_fail;

static void *cpu_alloc(void *arg) {
    uint32_t cpu = (uint32_t)(uintptr_t)arg;
    int i;
    host_set_cpu(cpu);
    for (i = 0; i < PAGES_PER; ++i) {
        uint64_t phys = pmm_alloc();
        uint64_t *cell;
        if (phys == 0) {
            fprintf(stderr, "cpu %u alloc failed at %d\n", cpu, i);
            g_fail = 1;
            return 0;
        }
        cell = (uint64_t *)(uintptr_t)bootinfo_phys_to_virt(phys);
        *cell = ((uint64_t)cpu << 32) | (uint32_t)i;
        g_pages[cpu][i] = phys;
    }
    return 0;
}

static int cmp_u64(const void *a, const void *b) {
    uint64_t x = *(const uint64_t *)a;
    uint64_t y = *(const uint64_t *)b;
    if (x < y)
        return -1;
    if (x > y)
        return 1;
    return 0;
}

static void heap_one_thread(void) {
    void *a;
    void *b;
    void *c;
    void *d;
    uint64_t before;
    a = kmalloc(32);
    b = kmalloc(64);
    c = kmalloc(128);
    if (!a || !b || !c) {
        fprintf(stderr, "heap alloc failed\n");
        g_fail = 1;
        return;
    }
    if (((uintptr_t)a & 15u) || ((uintptr_t)b & 15u) || ((uintptr_t)c & 15u)) {
        fprintf(stderr, "heap alignment\n");
        g_fail = 1;
    }
    memset(a, 0x11, 32);
    memset(b, 0x22, 64);
    memset(c, 0x33, 128);
    kfree(b);
    d = kmalloc(64);
    if (d != b) {
        fprintf(stderr, "heap did not reuse middle block\n");
        g_fail = 1;
    }
    kfree(a);
    kfree(c);
    kfree(d);
    before = heap_used_bytes();
    a = kmalloc(48);
    kfree(a);
    if (heap_used_bytes() != before) {
        fprintf(stderr, "heap accounting %llu vs %llu\n",
                (unsigned long long)heap_used_bytes(),
                (unsigned long long)before);
        g_fail = 1;
    }
    if (kmalloc(0) != 0) {
        fprintf(stderr, "kmalloc(0) should fail\n");
        g_fail = 1;
    }
}

static void *heap_cpu(void *arg) {
    uint32_t cpu = (uint32_t)(uintptr_t)arg;
    void *block[64];
    int i;
    host_set_cpu(cpu);
    for (i = 0; i < 64; ++i)
        block[i] = 0;
    for (i = 0; i < 200; ++i) {
        int slot = (i * 17 + (int)cpu) % 64;
        if (block[slot]) {
            memset(block[slot], 0, 16);
            kfree(block[slot]);
            block[slot] = 0;
        } else {
            uint64_t n = 16ull + (uint64_t)((i + cpu) % 200) * 16ull;
            block[slot] = kmalloc(n);
            if (block[slot])
                memset(block[slot], (int)cpu + 1, 16);
        }
    }
    for (i = 0; i < 64; ++i) {
        if (block[i])
            kfree(block[i]);
    }
    return 0;
}

int main(void) {
    pthread_t th[NCPU];
    uint64_t used0;
    uint64_t free0;
    uint64_t contig;
    uint64_t all[NCPU * PAGES_PER];
    int i;
    int cpu;

    host_set_cpu(0);
    pmm_init();
    used0 = pmm_used_pages();
    free0 = pmm_free_pages();
    for (cpu = 0; cpu < NCPU; ++cpu) {
        if (pthread_create(&th[cpu], 0, cpu_alloc, (void *)(uintptr_t)cpu) != 0) {
            fprintf(stderr, "pthread_create\n");
            return 1;
        }
    }
    for (cpu = 0; cpu < NCPU; ++cpu)
        pthread_join(th[cpu], 0);
    if (g_fail)
        return 1;
    for (cpu = 0; cpu < NCPU; ++cpu) {
        for (i = 0; i < PAGES_PER; ++i) {
            uint64_t phys = g_pages[cpu][i];
            uint64_t *cell = (uint64_t *)(uintptr_t)bootinfo_phys_to_virt(phys);
            uint64_t expect = ((uint64_t)cpu << 32) | (uint32_t)i;
            if (*cell != expect) {
                fprintf(stderr, "signature mismatch cpu %d i %d\n", cpu, i);
                return 1;
            }
            all[cpu * PAGES_PER + i] = phys;
        }
    }
    qsort(all, NCPU * PAGES_PER, sizeof(all[0]), cmp_u64);
    for (i = 1; i < NCPU * PAGES_PER; ++i) {
        if (all[i] == all[i - 1]) {
            fprintf(stderr, "duplicate phys %llu\n", (unsigned long long)all[i]);
            return 1;
        }
    }
    contig = pmm_alloc_contig(8);
    if (contig == 0) {
        fprintf(stderr, "contig alloc failed\n");
        return 1;
    }
    pmm_free_contig(contig, 8);
    for (cpu = 0; cpu < NCPU; ++cpu) {
        host_set_cpu((uint32_t)cpu);
        for (i = 0; i < PAGES_PER; ++i)
            pmm_free(g_pages[cpu][i]);
    }
    host_set_cpu(0);
    if (pmm_used_pages() != used0 || pmm_free_pages() != free0) {
        fprintf(stderr, "pmm accounting used %llu/%llu free %llu/%llu\n",
                (unsigned long long)pmm_used_pages(), (unsigned long long)used0,
                (unsigned long long)pmm_free_pages(), (unsigned long long)free0);
        return 1;
    }
    heap_init();
    heap_one_thread();
    for (cpu = 0; cpu < NCPU; ++cpu) {
        if (pthread_create(&th[cpu], 0, heap_cpu, (void *)(uintptr_t)cpu) != 0)
            return 1;
    }
    for (cpu = 0; cpu < NCPU; ++cpu)
        pthread_join(th[cpu], 0);
    if (g_fail)
        return 1;
    {
        void *cross = kmalloc(32);
        if (!cross)
            return 1;
        host_set_cpu(2);
        kfree(cross);
        host_set_cpu(0);
    }
    puts("test_pmm_heap_smp: ok");
    return 0;
}
