#include "syscall.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    uint8_t buf[81];
    memset(buf, 0xAB, sizeof(buf));
    if (syscall_write_term(buf, 81, 80u) != 0 || buf[80] != 0) {
        fprintf(stderr, "fail: n=80 into 81\n");
        return 1;
    }
    if (syscall_write_term(buf, 80, 80u) == 0) {
        fprintf(stderr, "fail: cap 80 accepted n=80\n");
        return 1;
    }
    if (syscall_write_term(buf, 81, 81u) == 0) {
        fprintf(stderr, "fail: n=81 accepted\n");
        return 1;
    }
    puts("test_sys_write: ok");
    return 0;
}
