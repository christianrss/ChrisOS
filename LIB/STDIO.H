#ifndef STDIO_H
#define STDIO_H
#include "STDDEF.H"
#include "STDARG.H"

int fopen(char *path);
void fclose(int fd);
int fread(int fd, char *buf, int n);
int fwrite(int fd, char *buf, int n);
int fsize(char *path);
int fexists(char *path);

int putchar(int c) {
    char b[2];
    b[0] = (char)c;
    b[1] = 0;
    fwrite(1, b, 1);
    return c;
}

int puts(char *s) {
    int n = 0;
    while (s[n]) n++;
    fwrite(1, s, n);
    putchar('\n');
    return 0;
}

int vsprintf(char *dst, char *fmt, va_list ap) {
    int i;
    int o;
    i = 0;
    o = 0;
    while (fmt[i]) {
        if (fmt[i] == '%' && fmt[i + 1]) {
            int spec = fmt[i + 1];
            int skip = 2;
            int v;
            if (spec == 'l' && fmt[i + 2] == 'd') {
                spec = 'd';
                skip = 3;
            }
            if (spec == '%') {
                dst[o] = '%';
                o = o + 1;
                i = i + 2;
            } else {
            v = (int)va_arg(ap, int);
            if (spec == 'd' || spec == 'i') {
                int n = v;
                char d[16];
                int nd = 0;
                if (n < 0) {
                    dst[o] = '-';
                    o = o + 1;
                    n = -n;
                }
                if (n == 0) {
                    d[nd] = '0';
                    nd = 1;
                }
                while (n > 0) {
                    d[nd] = (char)('0' + (n % 10));
                    nd = nd + 1;
                    n = n / 10;
                }
                while (nd) {
                    nd = nd - 1;
                    dst[o] = d[nd];
                    o = o + 1;
                }
                i = i + skip;
            } else if (spec == 'x' || spec == 'p') {
                int n;
                char d[16];
                int nd = 0;
                n = v;
                if (n < 0)
                    n = -n;
                if (spec == 'p') {
                    dst[o] = '0';
                    o = o + 1;
                    dst[o] = 'x';
                    o = o + 1;
                }
                if (n == 0) {
                    d[0] = '0';
                    nd = 1;
                }
                while (n) {
                    int h = n - (n / 16) * 16;
                    d[nd] = (char)(h < 10 ? '0' + h : 'a' + (h - 10));
                    nd = nd + 1;
                    n = n / 16;
                }
                while (nd) {
                    nd = nd - 1;
                    dst[o] = d[nd];
                    o = o + 1;
                }
                i = i + skip;
            } else if (spec == 'c') {
                dst[o] = (char)v;
                o = o + 1;
                i = i + skip;
            } else if (spec == 's') {
                char *s = (char *)v;
                int k = 0;
                if (!s)
                    s = "(null)";
                while (s[k]) {
                    dst[o] = s[k];
                    o = o + 1;
                    k = k + 1;
                }
                i = i + skip;
            } else {
                dst[o] = fmt[i];
                o = o + 1;
                i = i + 1;
            }
            }
        } else {
            dst[o] = fmt[i];
            o = o + 1;
            i = i + 1;
        }
    }
    dst[o] = 0;
    return o;
}

int sprintf(char *dst, char *fmt, ...) {
    va_list ap;
    int n;
    va_start(ap, fmt);
    n = vsprintf(dst, fmt, ap);
    va_end(ap);
    return n;
}

int printf(char *fmt, ...) {
    char b[128];
    va_list ap;
    int n;
    va_start(ap, fmt);
    n = vsprintf(b, fmt, ap);
    va_end(ap);
    fwrite(1, b, n);
    return n;
}
#endif
