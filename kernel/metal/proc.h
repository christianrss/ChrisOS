#ifndef CHRIS_PROC_H
#define CHRIS_PROC_H

#include <stdint.h>

#define PROC_MAX 32
#define PROC_KERNEL 0
#define PROC_VM_VIRT 0x02000000ull
#define PROC_HEAP_VIRT 0x04000000ull
#define PROC_FB_VIRT 0x06000000ull
#define PROC_STACK_VIRT 0x07F00000ull
#define PROC_LIB_VIRT 0x08000000ull
#define PROC_ST_FREE 0
#define PROC_ST_READY 1
#define PROC_ST_BLOCK_SOCK 2
#define PROC_ST_BLOCK_JOIN 3
#define PROC_ST_BLOCK_IRQ 4

typedef struct ProcFault {
    int valid;
    int pid;
    int tid;
    uint64_t cr2;
    uint64_t rip;
} ProcFault;

void proc_init(void);
int proc_create(const char *name);
void proc_destroy(int pid);
int proc_current(void);
void proc_switch(int pid);
uint64_t proc_cr3(int pid);
int proc_map_user(int pid, uint64_t virt, uint64_t phys, uint64_t flags);
void proc_on_tick(void);
int proc_slice_due(void);
void proc_slice_ack(void);
void proc_record_fault(int pid, int tid, uint64_t cr2, uint64_t rip);
const ProcFault *proc_last_fault(void);
int proc_alive(int pid);
int proc_runnable(int pid);
void proc_block(int pid, int why);
void proc_unblock(int pid);
void proc_unblock_why(int why);
uint64_t proc_page_phys(int pid, uint64_t virt);
uint8_t *proc_vm_ptr(int pid);
uint64_t proc_set_vm(int pid, uint64_t bytes);
int proc_commit(int pid, uint64_t virt);
int proc_fault_demand(int pid, uint64_t cr2);
void proc_release_user(int pid);
uint64_t proc_sbrk(int pid, uint64_t inc);
uint8_t *proc_fb_ptr(int pid, int pages);

#endif
