#ifndef CHRIS_CLVM_SYNC_H
#define CHRIS_CLVM_SYNC_H

/* Two VMs share numeric guest addresses. A mutex is (slot, address). */
static inline int clvm_sync_same(int slot_a, int addr_a, int slot_b, int addr_b) {
    return slot_a >= 0 && slot_a == slot_b && addr_a == addr_b;
}

#endif
