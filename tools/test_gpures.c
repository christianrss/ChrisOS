#include "gpures.h"

#include <stdio.h>

static int fail(const char *msg) {
    fprintf(stderr, "gpures: %s\n", msg);
    return 1;
}

int main(void) {
    GpuPool pool;
    int i;
    uint32_t a = 0;
    uint32_t b = 0;
    gpu_pool_init(&pool);
    if (gpu_res_alloc(&pool, 1, &a) != 0 || gpu_res_alloc(&pool, 1, &b) != 0) {
        return fail("alloc");
    }
    if (a == 0u || b == 0u || a == b) {
        return fail("ids");
    }
    if (gpu_res_release(&pool, 2, a) != -2) {
        return fail("owner");
    }
    if (gpu_res_release(&pool, 1, a) != 0 || gpu_res_get(&pool, a) != 0) {
        return fail("free");
    }
    gpu_res_release(&pool, 1, b);
    for (i = 0; i < 1000; ++i) {
        uint32_t id = 0;
        uint32_t ctx = 0;
        if (gpu_res_alloc(&pool, 1, &id) != 0 || gpu_ctx_alloc(&pool, 1, 2, &ctx) != 0) {
            return fail("cycle alloc");
        }
        if (gpu_res_release(&pool, 1, id) != 0 || gpu_ctx_release(&pool, 1, ctx) != 0) {
            return fail("cycle free");
        }
    }
    if (gpu_res_live(&pool) != 0 || gpu_ctx_live(&pool) != 0) {
        return fail("leak");
    }
    printf("test_gpures: ok\n");
    return 0;
}
