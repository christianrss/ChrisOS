#include "heap.h"
#include "bootinfo.h"
#include "panic.h"
#include "pmm.h"
#include "serial.h"
#include "spin.h"

#define HEAP_ALIGN          16ull
#define HEAP_USER_OFF       16ull
#define HEAP_ARENA_MAX      32
#define HEAP_MIN_ARENA_PAGES 16ull

struct heap_block {
    uint64_t size;
    uint32_t used;
    uint32_t pad;
};

struct heap_arena {
    uint8_t *base;
    uint64_t limit;
};

static struct heap_arena arenas[HEAP_ARENA_MAX];
static int narenas;
static int heap_ready;
static Spinlock heap_lock;

static uint64_t align_up(uint64_t value, uint64_t align) {
    return (value + align - 1ull) & ~(align - 1ull);
}

static struct heap_block *block_from_user(uint8_t *user) {
    return (struct heap_block *)(user - HEAP_USER_OFF);
}

static uint8_t *user_from_block(struct heap_block *block) {
    return (uint8_t *)block + HEAP_USER_OFF;
}

static struct heap_block *next_block(struct heap_block *block) {
    return (struct heap_block *)(user_from_block(block) + block->size);
}

static int block_in_arena(struct heap_arena *a, struct heap_block *block) {
    uint8_t *ptr;

    ptr = (uint8_t *)block;
    if (ptr < a->base) {
        return 0;
    }
    if (ptr + HEAP_USER_OFF > a->base + a->limit) {
        return 0;
    }
    return 1;
}

static struct heap_arena *arena_of_user(uint8_t *user) {
    int i;

    for (i = 0; i < narenas; ++i) {
        if (user >= arenas[i].base + HEAP_USER_OFF &&
            user < arenas[i].base + arenas[i].limit) {
            return &arenas[i];
        }
    }
    return 0;
}

static void heap_tally(uint64_t *used_bytes, uint64_t *free_bytes) {
    int i;
    struct heap_block *block;
    uint64_t used;
    uint64_t free_size;

    used = 0;
    free_size = 0;
    for (i = 0; i < narenas; ++i) {
        block = (struct heap_block *)arenas[i].base;
        while (block_in_arena(&arenas[i], block) &&
               (uint8_t *)block < arenas[i].base + arenas[i].limit) {
            if (block->used) {
                used += block->size;
            } else {
                free_size += block->size;
            }
            block = next_block(block);
        }
    }
    *used_bytes = used;
    *free_bytes = free_size;
}

static void coalesce_forward(struct heap_arena *a, struct heap_block *block) {
    struct heap_block *next;

    next = next_block(block);
    while (block_in_arena(a, next) &&
           (uint8_t *)next < a->base + a->limit &&
           next->used == 0) {
        block->size = block->size + HEAP_USER_OFF + next->size;
        next = next_block(block);
    }
}

static int add_arena_phys(uint64_t phys, uint64_t bytes) {
    struct heap_block *block;

    if (narenas == HEAP_ARENA_MAX || phys == 0 || bytes < HEAP_USER_OFF + HEAP_ALIGN) {
        return 0;
    }
    arenas[narenas].base = (uint8_t *)bootinfo_phys_to_virt(phys);
    arenas[narenas].limit = bytes;
    if (((uint64_t)arenas[narenas].base & (HEAP_ALIGN - 1ull)) != 0) {
        panic("heap: arena desalinhada");
    }
    block = (struct heap_block *)arenas[narenas].base;
    block->size = bytes - HEAP_USER_OFF;
    block->used = 0;
    block->pad = 0;
    narenas++;
    return 1;
}

static uint64_t keep_pages(void) {
    return PMM_KEEP / PMM_PAGE;
}

static int heap_grow(uint64_t need) {
    uint64_t pages;
    uint64_t available;
    uint64_t phys;

    pages = (need + HEAP_USER_OFF + PMM_PAGE - 1ull) / PMM_PAGE;
    if (pages < HEAP_MIN_ARENA_PAGES) {
        pages = HEAP_MIN_ARENA_PAGES;
    }
    if (pmm_free_pages() <= keep_pages()) {
        return 0;
    }
    available = pmm_free_pages() - keep_pages();
    if (pages > available) {
        pages = available;
    }
    if (pages < HEAP_MIN_ARENA_PAGES) {
        return 0;
    }
    phys = pmm_alloc_contig(pages);
    if (phys == 0) {
        phys = pmm_alloc_contig(HEAP_MIN_ARENA_PAGES);
        if (phys == 0) {
            return 0;
        }
        pages = HEAP_MIN_ARENA_PAGES;
    }
    return add_arena_phys(phys, pages * PMM_PAGE);
}

static int heap_claim_run(uint64_t phys, uint64_t pages, void *user) {
    uint64_t take;
    uint64_t budget;
    (void)user;

    if (narenas >= HEAP_ARENA_MAX) {
        return 0;
    }
    if (pmm_free_pages() <= keep_pages()) {
        return 0;
    }
    budget = pmm_free_pages() - keep_pages();
    take = pages;
    if (take > budget) {
        take = budget;
    }
    if (take < HEAP_MIN_ARENA_PAGES) {
        return 1;
    }
    if (pmm_claim_at(phys, take) == 0) {
        return 1;
    }
    if (!add_arena_phys(phys, take * PMM_PAGE)) {
        pmm_free_contig(phys, take);
        return 0;
    }
    return 1;
}

void heap_init(void) {
    uint64_t used;
    uint64_t free_size;

    spin_init(&heap_lock);
    narenas = 0;
    heap_ready = 0;

    if (pmm_free_pages() <= keep_pages()) {
        panic("heap_init: RAM insuficiente para KEEP");
    }
    pmm_foreach_free_run(heap_claim_run, 0);
    if (narenas == 0) {
        if (!heap_grow(HEAP_MIN_ARENA_PAGES * PMM_PAGE)) {
            panic("heap_init: nenhuma arena");
        }
    }
    heap_ready = 1;

    heap_tally(&used, &free_size);
    serial_puts("heap arenas=");
    serial_write_u64((uint64_t)narenas);
    serial_puts(" used=");
    serial_write_u64(used);
    serial_puts(" free=");
    serial_write_u64(free_size);
    serial_puts(" pmm_free=");
    serial_write_u64(pmm_free_pages() * PMM_PAGE);
    serial_puts("\n");
}

static void *kmalloc_in_arenas(uint64_t need) {
    int i;
    struct heap_block *block;
    struct heap_block *split;
    uint64_t leftover;

    for (i = 0; i < narenas; ++i) {
        block = (struct heap_block *)arenas[i].base;
        while (block_in_arena(&arenas[i], block) &&
               (uint8_t *)block < arenas[i].base + arenas[i].limit) {
            if (block->used == 0 && block->size >= need) {
                leftover = block->size - need;
                if (leftover >= HEAP_USER_OFF + HEAP_ALIGN) {
                    split = (struct heap_block *)(user_from_block(block) + need);
                    split->size = leftover - HEAP_USER_OFF;
                    split->used = 0;
                    split->pad = 0;
                    block->size = need;
                }
                block->used = 1;
                return user_from_block(block);
            }
            block = next_block(block);
        }
    }
    return 0;
}

void *kmalloc(uint64_t size) {
    uint64_t need;
    void *p;

    if (!heap_ready) {
        panic("kmalloc antes de heap_init");
    }
    if (size == 0) {
        return 0;
    }
    need = align_up(size, HEAP_ALIGN);
    /* heap lock then PMM lock. PMM must not allocate from the heap. */
    spin_lock(&heap_lock);
    p = kmalloc_in_arenas(need);
    if (!p && heap_grow(need)) {
        p = kmalloc_in_arenas(need);
    }
    spin_unlock(&heap_lock);
    if (!p) {
        serial_puts("kmalloc OOM need=");
        serial_write_u64(need);
        serial_puts("\n");
    }
    return p;
}

void kfree(void *ptr) {
    uint8_t *user;
    struct heap_block *block;
    struct heap_arena *a;

    if (ptr == 0) {
        return;
    }
    if (!heap_ready) {
        panic("kfree antes de heap_init");
    }
    user = (uint8_t *)ptr;
    if (((uint64_t)user & (HEAP_ALIGN - 1ull)) != 0) {
        panic("kfree ponteiro desalinhado");
    }
    spin_lock(&heap_lock);
    a = arena_of_user(user);
    if (a == 0) {
        spin_unlock(&heap_lock);
        panic("kfree fora da arena");
    }
    block = block_from_user(user);
    if (block->used == 0) {
        spin_unlock(&heap_lock);
        panic("kfree double-free");
    }
    block->used = 0;
    coalesce_forward(a, block);
    spin_unlock(&heap_lock);
}

uint64_t heap_used_bytes(void) {
    uint64_t used;
    uint64_t free_size;

    if (!heap_ready) {
        panic("heap_used_bytes antes de heap_init");
    }
    spin_lock(&heap_lock);
    heap_tally(&used, &free_size);
    spin_unlock(&heap_lock);
    return used;
}

uint64_t heap_free_bytes(void) {
    uint64_t used;
    uint64_t free_size;

    if (!heap_ready) {
        panic("heap_free_bytes antes de heap_init");
    }
    spin_lock(&heap_lock);
    heap_tally(&used, &free_size);
    spin_unlock(&heap_lock);
    return free_size;
}

uint64_t heap_arena_count(void) {
    uint64_t n;

    spin_lock(&heap_lock);
    n = (uint64_t)narenas;
    spin_unlock(&heap_lock);
    return n;
}

void heap_selftest(void) {
    uint8_t *a;
    uint8_t *b;
    uint8_t *c;
    uint8_t *d;
    uint64_t index;

    a = (uint8_t *)kmalloc(16);
    b = (uint8_t *)kmalloc(64);
    c = (uint8_t *)kmalloc(256);
    if (a == 0 || b == 0 || c == 0) {
        panic("heap_selftest: kmalloc inicial falhou");
    }
    if (a == b || b == c || a == c) {
        panic("heap_selftest: ponteiros repetidos");
    }
    if (((uint64_t)a & (HEAP_ALIGN - 1ull)) != 0 ||
        ((uint64_t)b & (HEAP_ALIGN - 1ull)) != 0 ||
        ((uint64_t)c & (HEAP_ALIGN - 1ull)) != 0) {
        panic("heap_selftest: alinhamento 16 falhou");
    }

    for (index = 0; index < 16; ++index) {
        a[index] = 0x11u;
    }
    for (index = 0; index < 64; ++index) {
        b[index] = 0x22u;
    }
    for (index = 0; index < 256; ++index) {
        c[index] = 0x33u;
    }

    kfree(b);
    d = (uint8_t *)kmalloc(64);
    if (d != b) {
        panic("heap_selftest: first-fit nao reutilizou o bloco do meio");
    }
    for (index = 0; index < 64; ++index) {
        d[index] = 0x44u;
    }
    for (index = 0; index < 16; ++index) {
        if (a[index] != 0x11u) {
            panic("heap_selftest: bloco 16 corrompido");
        }
    }
    for (index = 0; index < 256; ++index) {
        if (c[index] != 0x33u) {
            panic("heap_selftest: bloco 256 corrompido");
        }
    }

    serial_puts("heap selftest reuse ptr=");
    serial_write_hex((uint64_t)d);
    serial_puts("\nheap used=");
    serial_write_u64(heap_used_bytes());
    serial_puts(" free=");
    serial_write_u64(heap_free_bytes());
    serial_puts("\n");
}
