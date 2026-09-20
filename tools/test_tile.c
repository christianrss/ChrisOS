#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "job_host_stub.h"
#include "tile.h"

#define W 256
#define H 192

static uint32_t fb[W * H];

static uint32_t checksum(const uint32_t *buf, int n) {
    uint32_t sum = 0;
    int i;
    for (i = 0; i < n; ++i)
        sum ^= buf[i] + (uint32_t)(i * 131u);
    return sum;
}

static void run_clear(int workers) {
    job_host_reset();
    job_host_set_workers(workers);
    memset(fb, 0, sizeof(fb));
    tile_parallel_clear(fb, W, H, 0x00FF00FFu);
}

int main(void) {
    uint32_t a;
    uint32_t b;

    run_clear(1);
    a = checksum(fb, W * H);
    assert(fb[0] == 0x00FF00FFu);
    assert(fb[W * H - 1] == 0x00FF00FFu);

    run_clear(2);
    b = checksum(fb, W * H);
    assert(a == b);
    puts("test_tile: ok");
    return 0;
}
