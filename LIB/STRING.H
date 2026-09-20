#ifndef STRING_H
#define STRING_H
#include "STDDEF.H"

int strlen(char *s);
int strcmp(char *a, char *b);
char *strcpy(char *d, char *s);
void memcpy(char *d, char *s, int n);
void memmove(char *d, char *s, int n);
int strncmp(char *a, char *b, int n);
void memset(char *d, int c, int n);
int strcasecmp(char *a, char *b);
int strncasecmp(char *a, char *b, int n);
char *strdup(char *s);
char *strstr(char *h, char *n);
char *strncpy(char *d, char *s, int n);
char *strerror(int err);
char *strchr(char *s, int c);
char *strrchr(char *s, int c);
int memcmp(char *a, char *b, int n);
char *memchr(char *s, int c, int n);
char *strcat(char *d, char *s);
#endif
