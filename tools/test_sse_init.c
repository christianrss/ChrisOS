#include <assert.h>
#include <stdio.h>
#include "sse_init.h"

int main(void) {
    assert(sse_bsp_ready() == 0);
    sse_bsp_init();
    assert(sse_bsp_ready() == 1);
    puts("test_sse_init: ok");
    return 0;
}
