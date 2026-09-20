#ifndef CHRIS_NATIVE_LINK_H
#define CHRIS_NATIVE_LINK_H

#include "chriso.h"

#define NATIVE_USER_LOAD 0x400000ull

void native_elf_path(const char *src_path, char *out_path, int out_cap);
void native_image_free(ChrisoImage *img);
int native_link_write_elf(const ChrisoImage *img, const char *out_path);

#endif
