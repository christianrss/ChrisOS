/* doomgeneric-style backend for ChrisC / CLVM. Sound and net are stubs. */

int DG_ScreenW;
int DG_ScreenH;
char DG_Screen[64000];
char DG_Pal[768];
int DG_Ticks;
int DG_Running;

void DG_SetWindowTitle(char *title) {
    title = title;
}

void DG_Init() {
    int i;
    DG_ScreenW = 320;
    DG_ScreenH = 200;
    viewport(320, 200);
    i = 0;
    while (i < 768) {
        DG_Pal[i] = (char)((i / 3) & 255);
        i = i + 1;
    }
    setpal(DG_Pal);
    DG_Ticks = ticks();
    DG_Running = 1;
}

void DG_DrawFrame() {
    fb_blit(DG_Screen, DG_ScreenW, DG_ScreenH);
}

void DG_SleepMs(int ms) {
    wait(ms);
}

int DG_GetTicksMs() {
    return ticks();
}

int DG_GetKey() {
    if (key(1)) {
        DG_Running = 0;
        return 27;
    }
    if (key(17))
        return 119;
    if (key(31))
        return 115;
    if (key(30))
        return 97;
    if (key(32))
        return 100;
    if (key(57))
        return 32;
    return 0;
}

void I_Sound_stub() {
}

void I_Music_stub() {
}
