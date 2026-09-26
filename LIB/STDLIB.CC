#include "LIB/STDLIB.H"

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

float atof(char *s) {
    float v;
    float frac;
    float div;
    int i;
    int sign;
    v = 0.0;
    frac = 0.0;
    div = 1.0;
    i = 0;
    sign = 1;
    if (s[0] == '-') {
        sign = -1;
        i = i + 1;
    }
    while (s[i] >= '0' && s[i] <= '9') {
        v = v * 10.0 + (float)(s[i] - '0');
        i = i + 1;
    }
    if (s[i] == '.') {
        i = i + 1;
        while (s[i] >= '0' && s[i] <= '9') {
            frac = frac * 10.0 + (float)(s[i] - '0');
            div = div * 10.0;
            i = i + 1;
        }
        v = v + frac / div;
    }
    return (float)sign * v;
}

void exit(int c) {
    c = c;
    printf("exit %d\n", c);
    for (;;) {
        wait(60);
    }
}

void abort() {
    exit(1);
}

void *calloc(size_t n, size_t sz) {
    int bytes;
    char *p;
    int i;
    bytes = (int)n * (int)sz;
    p = malloc((size_t)bytes);
    if (!p)
        return 0;
    i = 0;
    while (i < bytes) {
        p[i] = 0;
        i = i + 1;
    }
    return p;
}

char *getenv(char *name) {
    name = name;
    return 0;
}

int isatty(int fd) {
    fd = fd;
    return 0;
}

int system(char *cmd) {
    cmd = cmd;
    return -1;
}

int close(int fd) {
    fclose(fd);
    return 0;
}

int mkdir(char *path, int mode) {
    path = path;
    mode = mode;
    return 0;
}

int stat(char *path, int st) {
    path = path;
    st = st;
    return -1;
}
