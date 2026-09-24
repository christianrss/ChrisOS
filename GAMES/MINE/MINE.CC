#include "LIB/BODY.CC"
#include "LIB/CLIP.CC"
#include "LIB/ANIM.CC"
#include "LIB/SIM.CC"
#include "LIB/HIT.CC"
#include "GAMES/MINE/STATE.CC"
#include "GAMES/MINE/CUBE.CC"
#include "GAMES/MINE/GEN.CC"
#include "GAMES/MINE/SKY.CC"
#include "GAMES/MINE/SAVE.CC"
#include "GAMES/MINE/AI.CC"
#include "GAMES/MINE/PLAY.CC"
#include "GAMES/MINE/UI.CC"

void paint_world() {
    int i;
    float bob;
    float y;
    float t;
    cam(g_act[0].x, g_act[0].y + 1.5, g_act[0].z, g_act[0].yaw, g_act[0].pitch);
    sky_frame();
    world();
    i = 1;
    while (i < 16) {
        if (g_act[i].kind != 0) {
            y = g_act[i].y;
            if (g_act[i].kind == 3) {
                t = (float)(ticks() % 40);
                bob = anim_eval1(0.0, 0.0, 40.0, 1.2, t);
                if (g_act[i].state == 2)
                    y = y + 3.0 + bob;
                else if (g_rain)
                    y = g_act[i].y;
                else
                    y = y + bob;
            }
            if (g_act[i].kind == 4)
                draw_mob(g_act[i].x, y, g_act[i].z, 0.0, 6);
            else if (g_act[i].kind == 5)
                draw_mob(g_act[i].x, y, g_act[i].z, 0.0, 16 + g_act[i].state);
            else
                draw_mob(g_act[i].x, y, g_act[i].z, (float)(i * 20), 10 + g_act[i].kind);
        }
        i = i + 1;
    }
    anim_apply(g_scene, ticks() % 16);
    scene_draw((int)g_act[0].x, (int)g_act[0].z, (int)g_act[0].yaw);
    phys_step();
}

void boot_gfx() {
    int i;
    cube_init();
    i = 0;
    while (i < 16) {
        anim_key(i, 70 + i, 8, 80, i * 10);
        i = i + 1;
    }
    g_scene = scene_add(0, 72, 8, 80, 0, 14);
    g_phys = phys_add(40, 360, 10, 10);
    i = 0;
    while (i < 768) {
        g_pal[i] = 0;
        i = i + 1;
    }
    g_pal[3] = 90;
    g_pal[4] = 150;
    g_pal[5] = 220;
    g_pal[6] = 8;
    g_pal[7] = 10;
    g_pal[8] = 36;
    g_pal[9] = 40;
    g_pal[10] = 46;
    g_pal[11] = 58;
    setpal(g_pal);
    i = 0;
    while (i < 64) {
        g_spr[i] = 15;
        g_pcm[i] = i * 3;
        i = i + 1;
    }
    i = 0;
    while (i < 32) {
        g_tile[i] = 2;
        i = i + 1;
    }
}

void main() {
    int prev;
    viewport(800, 600);
    boot_gfx();
    g_mode = 0;
    prev = 0;
    while (g_mode != 9) {
        if (g_mode == 0) {
            ui_menu();
        } else {
            if (key(1) && prev == 0) {
                if (g_mode == 1) {
                    g_mode = 2;
                    g_look = 0;
                } else if (g_mode == 2 || g_mode == 3) {
                    g_mode = 1;
                    g_look = 0;
                }
            }
            if (key(23) && prev == 0) {
                if (g_mode == 3)
                    g_mode = 1;
                else if (g_mode == 1)
                    g_mode = 3;
            }
            if (g_mode == 1 || g_mode == 5)
                play_step();
            paint_world();
            ui_hud();
            if (g_mode == 2)
                ui_pause();
            if (g_mode == 3)
                ui_inv();
        }
        prev = key(1) | key(23);
        wait(1);
    }
}
