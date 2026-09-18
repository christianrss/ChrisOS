#include "heap.h"
#include "bootinfo.h"
#include "panic.h"
#include "pmm.h"
#include "serial.h"

#define HEAP_PAGES     256ull
#define HEAP_ALIGN     16ull
#define HEAP_USER_OFF  16ull

struct heap_block {
    uint32_t size;
    uint32_t used;
};

static uint8_t *heap_base;
static uint64_t heap_limit;
static int heap_ready;

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

static int block_in_arena(struct heap_block *block) {
    uint8_t *ptr;

    ptr = (uint8_t *)block;
    if (ptr < heap_base) {
        return 0;
    }
    if (ptr + HEAP_USER_OFF > heap_base + heap_limit) {
        return 0;
    }
    return 1;
}

static void heap_tally(uint64_t *used_bytes, uint64_t *free_bytes) {
    struct heap_block *block;
    uint64_t used;
    uint64_t free_size;

    used = 0;
    free_size = 0;
    block = (struct heap_block *)heap_base;
    while (block_in_arena(block) && (uint8_t *)block < heap_base + heap_limit) {
        if (block->used) {
            used += block->size;
        } else {
            free_size += block->size;
        }
        block = next_block(block);
    }
    *used_bytes = used;
    *free_bytes = free_size;
}

static void coalesce_forward(struct heap_block *block) {
    struct heap_block *next;

    next = next_block(block);
    while (block_in_arena(next) &&
           (uint8_t *)next < heap_base + heap_limit &&
           next->used == 0) {
        block->size = (uint32_t)(block->size + HEAP_USER_OFF + next->size);
        next = next_block(block);
    }
}

void heap_init(void) {
    uint64_t first;
    uint64_t phys;
    uint64_t index;
    struct heap_block *block;
    uint64_t payload;

    first = 0;
    for (index = 0; index < HEAP_PAGES; ++index) {
        phys = pmm_alloc();
        if (phys == 0) {
            panic("heap_init: pmm_alloc falhou");
        }
        if (index == 0) {
            first = phys;
        } else if (phys != first + index * PMM_PAGE) {
            panic("heap_init: paginas nao contiguas");
        }
    }

    heap_base = (uint8_t *)bootinfo_phys_to_virt(first);
    heap_limit = HEAP_PAGES * PMM_PAGE;
    if (((uint64_t)heap_base & (HEAP_ALIGN - 1ull)) != 0) {
        panic("heap_init: arena desalinhada");
    }

    payload = heap_limit - HEAP_USER_OFF;
    block = (struct heap_block *)heap_base;
    block->size = (uint32_t)payload;
    block->used = 0;
    heap_ready = 1;

    serial_puts("heap arena phys=");
    serial_write_hex(first);
    serial_puts(" virt=");
    serial_write_hex((uint64_t)heap_base);
    serial_puts(" bytes=");
    serial_write_u64(heap_limit);
    serial_puts("\n");
}

void *kmalloc(uint64_t size) {
    struct heap_block *block;
    struct heap_block *split;
    uint64_t need;
    uint64_t leftover;

    if (!heap_ready) {
        panic("kmalloc antes de heap_init");
    }
    if (size == 0) {
        return 0;
    }
    need = align_up(size, HEAP_ALIGN);
    if (need > 0xffffffffull) {
        serial_puts("kmalloc OOM size demasiado grande\n");
        return 0;
    }

    block = (struct heap_block *)heap_base;
    while (block_in_arena(block) && (uint8_t *)block < heap_base + heap_limit) {
        if (block->used == 0 && block->size >= (uint32_t)need) {
            leftover = (uint64_t)block->size - need;
            if (leftover >= HEAP_USER_OFF + HEAP_ALIGN) {
                split = (struct heap_block *)(user_from_block(block) + need);
                split->size = (uint32_t)(leftover - HEAP_USER_OFF);
                split->used = 0;
                block->size = (uint32_t)need;
            }
            block->used = 1;
            return user_from_block(block);
        }
        block = next_block(block);
    }

    serial_puts("kmalloc OOM need=");
    serial_write_u64(need);
    serial_puts("\n");
    return 0;
}

void kfree(void *ptr) {
    uint8_t *user;
    struct heap_block *block;

    if (ptr == 0) {
        return;
    }
    if (!heap_ready) {
        panic("kfree antes de heap_init");
    }
    user = (uint8_t *)ptr;
    if (user < heap_base + HEAP_USER_OFF || user >= heap_base + heap_limit) {
        panic("kfree fora da arena");
    }
    if (((uint64_t)user & (HEAP_ALIGN - 1ull)) != 0) {
        panic("kfree ponteiro desalinhado");
    }
    block = block_from_user(user);
    if (block->used == 0) {
        panic("kfree double-free");
    }
    block->used = 0;
    coalesce_forward(block);
}

uint64_t heap_used_bytes(void) {
    uint64_t used;
    uint64_t free_size;

    if (!heap_ready) {
        panic("heap_used_bytes antes de heap_init");
    }
    heap_tally(&used, &free_size);
    return used;
}

uint64_t heap_free_bytes(void) {
    uint64_t used;
    uint64_t free_size;

    if (!heap_ready) {
        panic("heap_free_bytes antes de heap_init");
    }
    heap_tally(&used, &free_size);
    return free_size;
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
