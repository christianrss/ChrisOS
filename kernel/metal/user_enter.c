#include "user_enter.h"

#include "gdt.h"
#include "syscall.h"

void enter_user(uint64_t rip, uint64_t rsp_user) {
    uint64_t cs = (uint64_t)(GDT_USER_CODE | 3u);
    uint64_t ss = (uint64_t)(GDT_USER_DATA | 3u);
    uint64_t rflags = 0x202ull;

    syscall_set_kernel_return((uint64_t)(uintptr_t)__builtin_return_address(0));

    __asm__ volatile (
        "pushq %[ss]\n"
        "pushq %[rsp]\n"
        "pushq %[rf]\n"
        "pushq %[cs]\n"
        "pushq %[ip]\n"
        "iretq\n"
        :
        : [ss] "r"(ss), [rsp] "r"(rsp_user), [rf] "r"(rflags),
          [cs] "r"(cs), [ip] "r"(rip)
        : "memory"
    );
}
