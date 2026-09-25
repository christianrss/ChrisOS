#ifndef CHRISOS_BUILDID_H
#define CHRISOS_BUILDID_H

#include <stdint.h>

const char *build_git(void);
const char *build_id(void);
const char *build_date(void);
const char *build_compiler(void);
const char *build_kernel_sha256(void);
/* Writes the boot identity lines. Returns the length, or -1 if the
 * buffer cannot hold the text and a terminator. */
int build_info_format(char *dst, uint32_t cap);
void build_info_log(void);

#endif
