#ifndef CHRISOS_MM_H
#define CHRISOS_MM_H

#include <stdint.h>

#define MM_PRESENT (1ull << 0)
#define MM_WRITE   (1ull << 1)
#define MM_USER    (1ull << 2)
#define MM_PWT     (1ull << 3)
#define MM_PCD     (1ull << 4)
#define MM_NX      (1ull << 63)

void mm_init(void);
void mm_selftest(void);
uint64_t mm_kernel_cr3(void);
uint64_t mm_clone_kernel_space(void);
void mm_switch(uint64_t cr3_phys);
int mm_map_cr3(uint64_t cr3_phys, uint64_t virt, uint64_t phys, uint64_t flags);
void map_4k(uint64_t virt, uint64_t phys, uint64_t flags);
void map_4k_nosync(uint64_t virt, uint64_t phys, uint64_t flags);
/* Drop a kernel mapping. Does not free the physical page. Pair with
 * mm_tlb_shootdown before that page is reused. */
void unmap_4k(uint64_t virt);
/* Clear one leaf in a specific address space. Does not free the frame. */
void mm_unmap_cr3(uint64_t cr3_phys, uint64_t virt);
void mm_flush_tlb(void);
/* Ask every online CPU to invlpg the latest unmap. Remote CPUs are poked
 * with a LAPIC IPI (vector 0xF0) and also notice it from mm_tlb_poll.
 * A CR3 reload here killed the machine. A CPU that never answers is fenced:
 * it leaves the online set and its frames are not reused until it halts.
 * Returns 0 when reuse is safe, -1 when the caller must quarantine. */
void mm_tlb_shootdown(void);
int mm_tlb_shootdown_range(uint64_t virt, uint64_t bytes);
/* Hold frames that still might be cached on a fenced CPU. */
void mm_tlb_quarantine(uint64_t phys, uint64_t pages);
void mm_tlb_reap(void);
/* Called from idle/worker paths so a CPU acknowledges a shootdown.
 * mm_tlb_poll_cpu uses the index assigned at boot. mm_tlb_poll derives it
 * from the stack, which is what an IPI handler has. */
void mm_tlb_poll_cpu(uint32_t cpu);
void mm_tlb_poll(void);
void *map_mmio_page(uint64_t phys);
void *mm_lapic_virt(void);
uint64_t mm_virt_to_phys(uint64_t virt);
/* Walk an address space. Returns 0 on success. *phys includes the page offset.
 * *flags is the leaf entry. Rejects non-present walks. */
int mm_translate(uint64_t cr3_phys, uint64_t virt, uint64_t *phys, uint64_t *flags);
/* Free a process PML4 and the user-half intermediate tables. Leaf frames
 * stay owned by the process page list. Does not free the kernel half. */
void mm_free_user_space(uint64_t cr3_phys);

#endif
