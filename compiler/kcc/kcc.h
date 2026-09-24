/* LEARN:SH16-15 */
#ifndef CHRIS_KCC_H
#define CHRIS_KCC_H

#include "chriso.h"

typedef struct KccDiag {
    char file[96];
    int line;
    int column;
    int severity;
    char message[160];
} KccDiag;

int kcc_compile_source(const char *src, ChrisoImage *out);
int kcc_compile_named(const char *file, const char *src, ChrisoImage *out);
const KccDiag *kcc_last_error(void);

#endif
