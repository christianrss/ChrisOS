/*
 * Doom bring-up on ChrisC: doomgeneric DG_* loop, 320x200 paletted blit,
 * WAD-in-RAM (PLAYPAL if present). Escape quits. WASD/space are keys.
 */
#include "LIB/STDIO.H"
#include "LIB/STDLIB.H"
#include "LIB/STRING.H"

int screen_w;
int screen_h;
char screen[64000];
char pal[768];
int running;
int wad_ok;
int px;
int py;

int rd32(char *p) {
    int v;
    v = p[0] & 255;
    v = v | ((p[1] & 255) << 8);
    v = v | ((p[2] & 255) << 16);
    v = v | ((p[3] & 255) << 24);
    return v;
}

void load_wad() {
    int fd;
    int n;
    int i;
    int sz;
    char head[16];
    char *wad;
    int num;
    int dir;
    char *wp;
    wad_ok = 0;
    wp = "GAMES/DOOM/DOOM1.WAD";
    fd = fopen(wp);
    if (fd < 0) {
        wp = "GAMES/DOOM1.WAD";
        fd = fopen(wp);
    }
    if (fd < 0)
        return;
    n = fread(fd, head, 12);
    if (n < 12) {
        fclose(fd);
        return;
    }
    if (head[0] != 'I' && head[0] != 'P') {
        fclose(fd);
        return;
    }
    num = rd32(head + 4);
    dir = rd32(head + 8);
    sz = fsize(wp);
    if (sz < 16) {
        fclose(fd);
        return;
    }
    wad = malloc(sz);
    if (!wad) {
        fclose(fd);
        return;
    }
    fseek(fd, 0);
    n = fread(fd, wad, sz);
    fclose(fd);
    i = 0;
    while (i < num) {
        int off;
        int sz;
        char *e;
        e = wad + dir + i * 16;
        off = rd32(e);
        sz = rd32(e + 4);
        if (e[8] == 'P' && e[9] == 'L' && e[10] == 'A' && e[11] == 'Y' &&
            e[12] == 'P' && e[13] == 'A' && e[14] == 'L' && sz >= 768) {
            memcpy(pal, wad + off, 768);
            wad_ok = 1;
        }
        i = i + 1;
    }
    free(wad);
}

void draw_frame() {
    int y;
    int x;
    int i;
    i = 0;
    y = 0;
    while (y < screen_h) {
        x = 0;
        while (x < screen_w) {
            int c;
            c = ((x + px) >> 2) & 15;
            c = c + (((y + py) >> 2) & 15) * 16;
            if (y > 8 && y < 24 && x > 8 && x < 80)
                c = 4;
            screen[i] = (char)c;
            i = i + 1;
            x = x + 1;
        }
        y = y + 1;
    }
    setpal(pal);
    fb_blit(screen, screen_w, screen_h);
    text(8, 8, "ChrisOS Doom demo", 0x00FFFFFF);
    text(8, 24, "Esc quit  WASD move", 0x00E0E0E0);
}

void doomgeneric_Create() {
    int i;
    screen_w = 320;
    screen_h = 200;
    viewport(320, 200);
    i = 0;
    while (i < 256) {
        pal[i * 3] = (char)i;
        pal[i * 3 + 1] = (char)(i / 2);
        pal[i * 3 + 2] = (char)(i / 3);
        i = i + 1;
    }
    load_wad();
    setpal(pal);
    px = 0;
    py = 0;
    running = 1;
}

void doomgeneric_Tick() {
    if (key(1))
        running = 0;
    if (key(17))
        py = py - 2;
    if (key(31))
        py = py + 2;
    if (key(30))
        px = px - 2;
    if (key(32))
        px = px + 2;
    if (key(57))
        px = px + 8;
    draw_frame();
    wait(16);
}

void main() {
    doomgeneric_Create();
    while (running) {
        doomgeneric_Tick();
    }
}
