#ifndef STDLIB_H
#define STDLIB_H
#include "STDDEF.H"

void *malloc(size_t n);
void free(void *p);
void *realloc(void *p, size_t n);

int abs(int x) {
    if (x < 0) return -x;
    return x;
}

int atoi(char *s) {
    int v = 0;
    int i = 0;
    int sign = 1;
    if (s[0] == '-') {
        sign = -1;
        i++;
    }
    while (s[i] >= '0' && s[i] <= '9') {
        v = v * 10 + (s[i] - '0');
        i++;
    }
    return sign * v;
}
#endif
