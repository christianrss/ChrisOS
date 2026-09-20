#ifndef STDIO_H
#define STDIO_H
#include "STDDEF.H"
#include "STDARG.H"

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#define EOF (-1)
#define FILE int
#define stdin 0
#define stdout 1
#define stderr 2

int fopen(char *path);
void fclose(int fd);
int fread(int fd, char *buf, int n);
int fwrite(int fd, char *buf, int n);
int fsize(char *path);
int fexists(char *path);
int fseek(int fd, int off);
int ftell(int fd);
int sscanf(char *s, char *fmt, ...);
int vsnprintf(char *dst, int n, char *fmt, va_list ap);
int vfprintf(int fd, char *fmt, va_list ap);
int fflush(int fd);
int remove(char *path);
int rename(char *oldn, char *newn);
#define fileno(x) (x)

int putchar(int c);
int puts(char *s);
int vsprintf(char *dst, char *fmt, va_list ap);
int sprintf(char *dst, char *fmt, ...);
int snprintf(char *dst, int n, char *fmt, ...);
int printf(char *fmt, ...);
int fprintf(int fd, char *fmt, ...);
#endif
