#include "graphics.h"

// R - 4 bits
// G - 5 bits
// B - 4 bits
int rgb(int r, int g, int b) {
    return r << 11 | g << 5 | b;
}

/* LEARN:P06 - RGB565, índices estilo CGA */
unsigned short palette16[16] = {
    0x0000, /*  0 black   */
    0x0010, /*  1 navy    */
    0x0400, /*  2 green   */
    0x0410, /*  3 teal    */
    0x8000, /*  4 maroon  */
    0x8010, /*  5 purple  */
    0x8400, /*  6 olive   */
    0xC618, /*  7 silver  */
    0x8410, /*  8 gray    */
    0x001F, /*  9 blue    */
    0x07E0, /* 10 lime    */
    0x07FF, /* 11 aqua    */
    0xF800, /* 12 red     */
    0xF81F, /* 13 fuchsia */
    0xFFE0, /* 14 yellow  */
    0xFFFF  /* 15 white   */
};

void PutPixel(int x, int y, int color) {
    VBEInfoBlock* VBE = (VBEInfoBlock*) VBEInfoAddress;
    unsigned short* buffer = (unsigned short*) ScreenBufferAddress;
    int index;
    if (color < 0 || color > 15) {
        return;
    }
    if (x < 0 || y < 0 || x >= VBE->x_resolution || y >= VBE->y_resolution) {
        return;
    }
    index = y * VBE->x_resolution + x;
    buffer[index] = palette16[color];
}

void Fill(int x, int y, int width, int height, int color) {
    int j, i;
    for (j = 0; j < height; j++) {
        for (i = 0; i < width; i++) {
            PutPixel(x + i, y + j , color);
        }
    }
}

void Draw(int x, int y, int r, int g, int b) {
    VBEInfoBlock* VBE = (VBEInfoBlock*) VBEInfoAddress;
    unsigned short* buffer = (unsigned short*) ScreenBufferAddress;

    /* LEARN:P02 clip */
    if (x < 0 || y < 0) {
        return;
    }
    if (x >= VBE->x_resolution || y >= VBE->y_resolution) {
        return;
    }

    int index = y * VBE->x_resolution + x;
    *(buffer + index) = rgb(r, g, b);
}

void ClearScreen(int r, int g, int b) {
    VBEInfoBlock* VBE = (VBEInfoBlock*) VBEInfoAddress;
    for (int y = 0; y < VBE->y_resolution; y++) {
        for (int x = 0; x < VBE->x_resolution; x++) {
            Draw(x, y, r, g, b);
        }
    }
}

void DrawRect(int x, int y, int width, int height, int r, int g, int b) {
    for (int j = y; j < (y + height); j++) {
        for (int i = x; i < (x + width); i++) {
            Draw(i, j, r, g, b);
        }
    }
}

void DrawCharacter(int (*f)(int, int), int font_width, int font_height, char character, int x, int y, int r, int g, int b) {
    for (int j = 0; j < font_height; j++) {
        unsigned int row = (*f)((int)(character), j);
        int shift = font_width - 1;
        int bit_val = 0;

        for (int i = 0; i < font_width; i++) {
            bit_val = (row >> shift) & 0b00000000000000000000000000000001;
            if (bit_val == 1)
                Draw(x + i, y + j, r, g, b);

            shift -= 1;
        }
    }
}

void DrawString(int (*f)(int, int), int font_width, int font_height, char* string, int x, int y, int r, int g, int b) {
    int i = 0, j = 0;

    for (int k  = 0; *(string + k) != 0; k++) {
        if (*(string + k) != '\n')
            DrawCharacter(f, font_width, font_height, *(string + k), x + i, y + j, r, g, b);

            i += font_width - (font_width / 5);

            if (*(string + k) == '\n') {
                i = 0;
                j += font_height;
            }
    }
}

void DrawMouse(int x, int y, int r, int g, int b) {
    int mouse[] = {
        0b11111111111,
        0b11111111110,
        0b11111111100,
        0b11111111000,
        0b11111110000,
        0b11111100000,
        0b11111000000,
        0b11110000000,
        0b11100000000,
        0b11000000000,
        0b10000000000
    };

    int mouse_width = 10, mouse_height = 10;
    for (int j = 0; j < mouse_height; j++) {
        unsigned int row = mouse[j];
        int shift = mouse_width - 1;
        int bit_val = 0;

        for (int i = 0; i < mouse_width; i++) {
            bit_val = (row >> shift) & 0b00000000000000000000000000000001;
            if (bit_val == 1)
                Draw(x + i, y + j, r, g, b);

            shift -= 1;
        }
    }
}

void DrawCircle(int x, int y, int radius, int r, int g, int b) {
    int rr = radius * radius;

    for (int j = -radius; j < radius; j++) {
        for (int i = -radius; i < radius; i++) {
            if ((i * i + j * j) <= rr)
                Draw(x + i, y + j, r, g, b);
        }
    }
}

void Flush() {
    VBEInfoBlock* VBE = (VBEInfoBlock*) VBEInfoAddress;
    unsigned int count = (unsigned int)VBE->x_resolution * (unsigned int)VBE->y_resolution;
    unsigned int* dst = (unsigned int*)VBE->screen_ptr;
    unsigned int* src = (unsigned int*)ScreenBufferAddress;
    unsigned int i;

    /* LEARN:P05 - 2 pixels (4 bytes) por iteração; 640*480 é par */
    for (i = 0; i < (count / 2); i++) {
        dst[i] = src[i];
    }

}