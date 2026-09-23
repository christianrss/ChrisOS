#ifndef CHRIS_CLVM_VM_H
#define CHRIS_CLVM_VM_H

#include <stddef.h>
#include <stdint.h>
#include "clvm.h"

#define CLVM_STACK_MAX 256
#define CLVM_CALL_MAX 64
#define CLVM_MEMORY_SIZE (1024u * 1024u)

typedef enum ClvmState {
    CLVM_READY = 0,
    CLVM_RUNNING,
    CLVM_WAITING,
    CLVM_HALTED,
    CLVM_FAULTED
} ClvmState;

typedef enum ClvmFault {
    CLVM_FAULT_NONE = 0,
    CLVM_FAULT_PC,
    CLVM_FAULT_OPCODE,
    CLVM_FAULT_TRUNCATED,
    CLVM_FAULT_STACK_UNDERFLOW,
    CLVM_FAULT_STACK_OVERFLOW,
    CLVM_FAULT_CALL_UNDERFLOW,
    CLVM_FAULT_CALL_OVERFLOW,
    CLVM_FAULT_DIV_ZERO,
    CLVM_FAULT_DIV_OVERFLOW,
    CLVM_FAULT_BAD_ADDRESS,
    CLVM_FAULT_BAD_JUMP,
    CLVM_FAULT_BAD_SYS
} ClvmFault;

typedef enum ClvmStepResult {
    CLVM_STEP_SLICE = 0,
    CLVM_STEP_YIELD,
    CLVM_STEP_HALT,
    CLVM_STEP_FAULT
} ClvmStepResult;

struct ClvmVm;
typedef int (*ClvmSysFn)(struct ClvmVm *vm, int32_t id, void *user);

typedef struct ClvmVm {
    const uint8_t *code;
    uint32_t code_size;
    uint32_t pc;
    int64_t stack[CLVM_STACK_MAX];
    uint16_t sp;
    uint32_t calls[CLVM_CALL_MAX];
    uint16_t csp;
    uint8_t *memory;
    uint64_t mem_size;
    uint8_t mem_owned;
    uint32_t wake_tick;
    uint64_t executed;
    ClvmState state;
    ClvmFault fault;
    uint32_t fault_pc;
    ClvmSysFn sys;
    void *sys_user;
    int64_t print_ring[8];
    uint8_t print_n;
    uint8_t safepoint;
    uint64_t heap_off;
    int64_t il_loc[32];
    int64_t il_arg[16];
    void (*on_safepoint)(struct ClvmVm *vm);
    int64_t tls[16];
    int32_t join_wait;
} ClvmVm;

void clvm_vm_init(ClvmVm *vm, const ClvmImage *image,
                  ClvmSysFn sys, void *sys_user);
void clvm_vm_set_memory(ClvmVm *vm, uint8_t *mem, uint64_t size);
void clvm_vm_wake(ClvmVm *vm, uint32_t now);
void clvm_vm_wait(ClvmVm *vm, uint32_t wake_tick);
int clvm_vm_push(ClvmVm *vm, int32_t value);
int clvm_vm_pop(ClvmVm *vm, int32_t *value);
int clvm_vm_push64(ClvmVm *vm, int64_t value);
int clvm_vm_pop64(ClvmVm *vm, int64_t *value);
ClvmStepResult clvm_step(ClvmVm *vm, uint32_t budget);
const char *clvm_fault_text(ClvmFault fault);
int clvm_guest_malloc(ClvmVm *vm, uint64_t n, uint64_t *out);
int clvm_guest_free(ClvmVm *vm, uint64_t p);
int clvm_guest_realloc(ClvmVm *vm, uint64_t p, uint64_t n, uint64_t *out);
int clvm_guest_setjmp(ClvmVm *vm, uint64_t addr);
int clvm_guest_longjmp(ClvmVm *vm, uint64_t addr, int64_t val);

#endif
