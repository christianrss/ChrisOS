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
/* Block until every online CPU has reloaded CR3 after the latest unmap. */
void mm_tlb_shootdown(void);
/* Called from idle/worker paths so a CPU acknowledges a shootdown. */
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
