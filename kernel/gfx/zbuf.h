#ifndef CHRIS_ZBUF_H
#define CHRIS_ZBUF_H

#include <stdint.h>

#define ZBUF_MAX_W 1920
#define ZBUF_MAX_H 1080
#define ZBUF_FAR   0xFFFFFFFFu

void zbuf_bind(uint32_t *external);
void zbuf_set_size(int width, int height);
int zbuf_width(void);
int zbuf_height(void);
void zbuf_clear(void);
int zbuf_test(int x, int y, uint32_t z);

#endif
