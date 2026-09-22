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
  int t;
  t = (ms * 60) / 1000;
  if (t < 1) {
    t = 1;
  }
  wait(t);
}

int DG_GetTicksMs() {
  return (ticks() * 1000) / 60;
}

/* doomgeneric polls this; Chris input uses I_StartTic → D_PostEvent instead. */
int DG_GetKey(int *pressed, int *key) {
  if (pressed) {
    *pressed = 0;
  }
  if (key) {
    *key = 0;
  }
  return 0;
}

void I_Sound_stub() {
}

void I_Music_stub() {
}
