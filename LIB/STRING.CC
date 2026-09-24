#include "LIB/STRING.H"

int strlen(char *s) {
    int n;
    n = 0;
    while (s[n])
        n = n + 1;
    return n;
}

int strcmp(char *a, char *b) {
    int i;
    i = 0;
    while (a[i] && a[i] == b[i])
        i = i + 1;
    return a[i] - b[i];
}

char *strcpy(char *d, char *s) {
    int i;
    i = 0;
    while (s[i]) {
        d[i] = s[i];
        i = i + 1;
    }
    d[i] = 0;
    return d;
}

void memcpy(char *d, char *s, int n) {
    int i;
    i = 0;
    while (i < n) {
        d[i] = s[i];
        i = i + 1;
    }
}

void memmove(char *d, char *s, int n) {
    int i;
    /* Pointer compare keeps the full guest address. Casting to int keeps
     * 32 bits and reverses overlap direction at bit 31. */
    if (d < s) {
        i = 0;
        while (i < n) {
            d[i] = s[i];
            i = i + 1;
        }
    } else {
        i = n;
        while (i) {
            i = i - 1;
            d[i] = s[i];
        }
    }
}

int strncmp(char *a, char *b, int n) {
    int i;
    i = 0;
    while (i < n && a[i] && a[i] == b[i])
        i = i + 1;
    if (i == n)
        return 0;
    return a[i] - b[i];
}

void memset(char *d, int c, int n) {
    int i;
    i = 0;
    while (i < n) {
        d[i] = (char)c;
        i = i + 1;
    }
}

int fold(int c) {
    if (c >= 'A' && c <= 'Z')
        return c - 'A' + 'a';
    return c;
}

int strcasecmp(char *a, char *b) {
    int i;
    i = 0;
    while (a[i] && fold(a[i]) == fold(b[i]))
        i = i + 1;
    return fold(a[i]) - fold(b[i]);
}

int strncasecmp(char *a, char *b, int n) {
    int i;
    i = 0;
    while (i < n && a[i] && fold(a[i]) == fold(b[i]))
        i = i + 1;
    if (i == n)
        return 0;
    return fold(a[i]) - fold(b[i]);
}

char *strdup(char *s) {
    int n;
    char *d;
    n = strlen(s) + 1;
    d = malloc(n);
    if (!d)
        return 0;
    memcpy(d, s, n);
    return d;
}

char *strstr(char *h, char *n) {
    int i;
    int ln;
    ln = strlen(n);
    if (!ln)
        return h;
    i = 0;
    while (h[i]) {
        if (strncmp(h + i, n, ln) == 0)
            return h + i;
        i = i + 1;
    }
    return 0;
}

char *strncpy(char *d, char *s, int n) {
    int i;
    i = 0;
    while (i < n && s[i]) {
        d[i] = s[i];
        i = i + 1;
    }
    while (i < n) {
        d[i] = 0;
        i = i + 1;
    }
    return d;
}

char *strerror(int err) {
    err = err;
    return "error";
}

char *strchr(char *s, int c) {
    int i;
    i = 0;
    while (s[i]) {
        if (s[i] == (char)c)
            return s + i;
        i = i + 1;
    }
    if ((char)c == 0)
        return s + i;
    return 0;
}

char *strrchr(char *s, int c) {
    int i;
    char *last;
    last = 0;
    i = 0;
    while (s[i]) {
        if (s[i] == (char)c)
            last = s + i;
        i = i + 1;
    }
    if ((char)c == 0)
        return s + i;
    return last;
}

int memcmp(char *a, char *b, int n) {
    int i;
    i = 0;
    while (i < n) {
        if (a[i] != b[i])
            return a[i] - b[i];
        i = i + 1;
    }
    return 0;
}

char *memchr(char *s, int c, int n) {
    int i;
    i = 0;
    while (i < n) {
        if (s[i] == (char)c)
            return s + i;
        i = i + 1;
    }
    return 0;
}

char *strcat(char *d, char *s) {
    int i;
    int j;
    i = 0;
    while (d[i])
        i = i + 1;
    j = 0;
    while (s[j]) {
        d[i] = s[j];
        i = i + 1;
        j = j + 1;
    }
    d[i] = 0;
    return d;
}
