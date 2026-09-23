#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "gfx_fast.h"

/*
 * Present barrier: the compositor must blit a published front buffer, not
 * the live back buffer being drawn into. wait() copies back -> front.
 */
static int expect_u32(const char *what, uint32_t got, uint32_t want) {
    if (got == want) {
        return 1;
    }
    fprintf(stderr, "test_slot_front: %s got=%08x want=%08x\n", what,
            (unsigned)got, (unsigned)want);
    return 0;
}

int main(void) {
    uint32_t back[8];
    uint32_t front[8];
    int i;

    for (i = 0; i < 8; i++) {
        back[i] = 0x00110000u + (uint32_t)i;
        front[i] = 0x00aabbccu;
    }

    back[0] = 0x00ff0000u;
    if (!expect_u32("slice must not publish", front[0], 0x00aabbccu)) {
        return 1;
    }

    gfx_fast_copy_u32(front, back, 8);
    if (!expect_u32("wait publishes pixel 0", front[0], 0x00ff0000u) ||
        !expect_u32("wait publishes pixel 7", front[7], 0x00110007u)) {
        return 1;
    }

    back[3] = 0x0000ff00u;
    if (!expect_u32("draw after wait stays off-front", front[3], 0x00110003u)) {
        return 1;
    }

    gfx_fast_copy_u32(front, back, 8);
    if (!expect_u32("second wait publishes", front[3], 0x0000ff00u)) {
        return 1;
    }

    puts("test_slot_front: ok");
    return 0;
}
