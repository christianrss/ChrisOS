#include "graphics.h"

int start() {
    VBEInfoBlock* VBE = (VBEInfoBlock*) VBEInfoAddress;

    x = VBE->x_resolution / 2;
    y = VBE->y_resolution / 2;
    // String literals cannot be more than 61 characters.
    char str1[] = "Welcome to ChrisOS! \n\nText rendered by custom library.";
    char *p = str1;

    base = (unsigned int) &isr1;
    base12 = (unsigned int) &isr12;

    InitialiseMouse();
    InitialiseIDT();

    while(1) {
        ClearScreen(181.0f / 255.0f * 16.0f, 232.0f / 255.0f * 32.0f, 255.0f / 255.0f * 16.0f);
        DrawString(getArialCharacter, font_arial_width, font_arial_height, p, 100, 100, 0, 0, 0);

        //DrawRect(x, y, 10, 10, 0, 0, 0);
        DrawMouse(x, y, 16, 100.00 / 255.0 * 32, 100.0 / 255.0 * 16);

        Flush();
    }
}