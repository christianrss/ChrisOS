#ifndef STDLIB_H
#define STDLIB_H
#include "STDDEF.H"

void *malloc(size_t n);
void free(void *p);
void *realloc(void *p, size_t n);
int abs(int x);
int atoi(char *s);
float atof(char *s);
void exit(int c);
void abort();
void *calloc(size_t n, size_t sz);
char *getenv(char *name);
int mkdir(char *path, int mode);
int isatty(int fd);
int system(char *cmd);
#endif
