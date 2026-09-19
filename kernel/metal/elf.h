#ifndef CHRISOS_ELF_H
#define CHRISOS_ELF_H

#include <stdint.h>

int elf_load(const uint8_t *file, uint32_t nbytes, uint64_t *entry_out);

#endif
