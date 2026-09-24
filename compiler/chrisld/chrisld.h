/* LEARN:SH16-14 */
#ifndef CHRIS_CHRISLD_H
#define CHRIS_CHRISLD_H

#include <stdint.h>
#include "chriso.h"

#define CHRISLD_ELF_MAX (1024u * 1024u)

int chrisld_link(const ChrisoImage *img, uint64_t load_addr, void *out,
                 uint32_t cap, uint64_t *entry_out);
int chrisld_link_objects(const ChrisoImage *const *imgs, uint32_t n,
                         uint64_t load_addr, void *out, uint32_t cap,
                         uint64_t *entry_out);
int chrisld_validate(const void *elf, uint32_t n);

#endif
