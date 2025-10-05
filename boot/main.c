#include "graphics.h"

int start() {
    ClearScreen(181.0f / 255.0f * 16.0f, 232.0f / 255.0f * 32.0f, 255.0f / 255.0f * 16.0f);

    // String literals cannot be more than 61 characters.
    char str1[] = "Welcome to ChrisOS! \n\nText rendered by custom library.";
    char *p = str1;

    DrawString(getArialCharacter, font_arial_width, font_arial_height, p, 100, 100, 0, 0, 0);

    while(1);
}