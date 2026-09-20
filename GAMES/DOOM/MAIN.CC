/* Engine bring-up: doomgeneric Create → D_DoomMain, then Tick. */

char *dg_argv[4];

void D_DoomMain();
void doomgeneric_Tick();

void doomgeneric_Create() {
    dg_argv[0] = "doom";
    dg_argv[1] = "-iwad";
    dg_argv[2] = "GAMES/DOOM/DOOM1.WAD";
    myargc = 3;
    myargv = dg_argv;
    DG_Init();
    D_DoomMain();
}

void main() {
    int running;
    running = 1;
    doomgeneric_Create();
    while (running) {
        if (key(1))
            running = 0;
        doomgeneric_Tick();
        wait(16);
    }
}
