#ifndef STRING_H
#define STRING_H
#include "STDDEF.H"

size_t strlen(char *s) {
    size_t n = 0;
    while (s[n]) n++;
    return n;
}

int strcmp(char *a, char *b) {
    int i = 0;
    while (a[i] && a[i] == b[i]) i++;
    return (int)(unsigned char)a[i] - (int)(unsigned char)b[i];
}

char *strcpy(char *d, char *s) {
    int i = 0;
    while (s[i]) {
        d[i] = s[i];
        i++;
    }
    d[i] = 0;
    return d;
}

void *memcpy(void *d, void *s, size_t n) {
    char *dd = d;
    char *ss = s;
    size_t i;
    for (i = 0; i < n; i++) dd[i] = ss[i];
    return d;
}

void *memset(void *d, int c, size_t n) {
    char *dd = d;
    size_t i;
    for (i = 0; i < n; i++) dd[i] = (char)c;
    return d;
}
#endif
