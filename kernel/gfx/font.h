#ifndef CHRIS_FONT_H
#define CHRIS_FONT_H

#include <stdint.h>

extern const int font_arial_width;
extern const int font_arial_height;

uint32_t font_row(unsigned int character, int row);

#endif
