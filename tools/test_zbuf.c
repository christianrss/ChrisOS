#include <assert.h>
#include <stdio.h>
#include "zbuf.h"

int main(void) {
    zbuf_set_size(8, 8);
    zbuf_clear();
    assert(zbuf_test(3, 4, 500u) == 1);
    assert(zbuf_test(3, 4, 800u) == 0);
    assert(zbuf_test(3, 4, 200u) == 1);
    assert(zbuf_test(3, 4, 200u) == 0);
    assert(zbuf_test(-1, 0, 1u) == 0);
    assert(zbuf_test(0, 8, 1u) == 0);
    puts("test_zbuf: ok");
    return 0;
}
