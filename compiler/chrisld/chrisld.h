/* LEARN:SH16-14 */
#ifndef CHRIS_CHRISLD_H
#define CHRIS_CHRISLD_H

#include <stdint.h>
#include "chriso.h"

#define CHRISLD_ELF_MAX (256u * 1024u)

int chrisld_link(const ChrisoImage *img, uint64_t load_addr, void *out,
                 uint32_t cap, uint64_t *entry_out);

#endif
