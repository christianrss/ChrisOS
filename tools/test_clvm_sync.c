#include <stdio.h>
#include "clvm_sync.h"

int main(void) {
    if (!clvm_sync_same(1, 0x1234, 1, 0x1234))
        return 1;
    if (clvm_sync_same(1, 0x1234, 2, 0x1234))
        return 1;
    if (clvm_sync_same(-1, 0x1234, -1, 0x1234))
        return 1;
    if (clvm_sync_same(3, 0x10, 3, 0x11))
        return 1;
    puts("test_clvm_sync: ok");
    return 0;
}
