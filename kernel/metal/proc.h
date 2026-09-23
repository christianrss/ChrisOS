#ifndef CHRIS_PROC_H
#define CHRIS_PROC_H

#include <stdint.h>

#define PROC_MAX 32
#define PROC_KERNEL 0

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

#endif
