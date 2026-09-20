/* Video backend: 8-bit 320x200 into DG_Screen, then fb_blit. */

char *I_VideoBuffer;
int screenvisible;
int usegamma;
int vanilla_keyboard_mapping;
int screensaver_mode;
char *video_driver;
int screen_width;
int screen_height;
int screen_bpp;
int fullscreen;
int aspect_ratio_correct;
int show_diskicon;
int diskicon_readbytes;
float mouse_acceleration;
int mouse_threshold;
int usemouse;
int key_was[128];

void I_InitGraphics() {
    int i;
    DG_Init();
    I_VideoBuffer = DG_Screen;
    screenvisible = 1;
    vanilla_keyboard_mapping = 1;
    screen_width = 320;
    screen_height = 200;
    mouse_acceleration = 2.0;
    mouse_threshold = 10;
    i = 0;
    while (i < 128) {
        key_was[i] = 0;
        i = i + 1;
    }
}

void I_ShutdownGraphics() {
}

void I_SetPalette(char *palette) {
    setpal(palette);
}

int I_GetPaletteIndex(int r, int g, int b) {
    r = r;
    g = g;
    b = g;
    return 0;
}

void I_UpdateNoBlit() {
}

void I_FinishUpdate() {
    DG_DrawFrame();
}

void I_ReadScreen(char *scr) {
    int i;
    i = 0;
    while (i < 64000) {
        scr[i] = I_VideoBuffer[i];
        i = i + 1;
    }
}

void I_BeginRead() {
}

void I_EndRead() {
}

void I_SetWindowTitle(char *title) {
    DG_SetWindowTitle(title);
}

void I_CheckIsScreensaver() {
}

void I_DisplayFPSDots(int dots_on) {
    dots_on = dots_on;
}

void I_BindVideoVariables() {
}

void I_InitWindowTitle() {
}

void I_InitWindowIcon() {
}

void I_StartFrame() {
}

void I_EnableLoadingDisk() {
}

void I_GraphicsCheckCommandLine() {
}

void I_SetGrabMouseCallback(int func) {
    func = func;
}
