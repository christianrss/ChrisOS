void num(int n) {
    int v;
    if (n < 0)
        n = 0;
    if (n > 99)
        n = 99;
    v = n / 10;
    g_num[0] = 48 + v;
    g_num[1] = 48 + (n % 10);
    g_num[2] = 0;
}

int ptr_over(int x, int y, int w, int h) {
    int mx;
    int my;
    mx = mouse_x();
    my = mouse_y();
    if (mx < x || my < y)
        return 0;
    if (mx >= x + w || my >= y + h)
        return 0;
    return 1;
}

int hit_btn(int x, int y, int w, int h) {
    if (ptr_over(x, y, w, h) == 0)
        return 0;
    return mouse_btn() & 1;
}

void paint_btn(int x, int y, int w, int h) {
    if (ptr_over(x, y, w, h))
        fillrgb(x, y, w, h, 0x00C4842A);
    else
        fillrgb(x, y, w, h, 0x00221C18);
}

int btn_ink(int x, int y, int w, int h) {
    if (ptr_over(x, y, w, h))
        return 0x001C1814;
    return 0x00F3EEE4;
}

void ui_menu() {
    fillrgb(0, 0, 800, 600, 0x00181014);
    text(280, 120, "Mine Chris", 0x00F3EEE4);
    paint_btn(280, 200, 220, 32);
    text(300, 208, "Jogar", btn_ink(280, 200, 220, 32));
    paint_btn(280, 248, 220, 32);
    text(300, 256, "Continuar", btn_ink(280, 248, 220, 32));
    paint_btn(280, 296, 220, 32);
    text(300, 304, "Sair", btn_ink(280, 296, 220, 32));
    if (hit_btn(280, 200, 220, 32)) {
        if (g_built == 0) {
            gen_world();
            boot_actors();
        }
        g_mode = 1;
    }
    if (hit_btn(280, 248, 220, 32)) {
        if (g_built == 0) {
            gen_world();
            boot_actors();
        }
        save_read();
        g_mode = 1;
    }
    if (hit_btn(280, 296, 220, 32))
        g_mode = 9;
}

void ui_pause() {
    fillrgb(240, 140, 300, 280, 0x00221C18);
    text(300, 160, "Pausa", 0x00F3EEE4);
    paint_btn(280, 200, 220, 28);
    text(300, 206, "Continuar", btn_ink(280, 200, 220, 28));
    paint_btn(280, 236, 220, 28);
    text(300, 242, "Checkpoint", btn_ink(280, 236, 220, 28));
    paint_btn(280, 272, 220, 28);
    text(300, 278, "Inventario", btn_ink(280, 272, 220, 28));
    paint_btn(280, 308, 220, 28);
    text(300, 314, "Sair da fase", btn_ink(280, 308, 220, 28));
    if (hit_btn(280, 200, 220, 28))
        g_mode = 1;
    if (hit_btn(280, 236, 220, 28)) {
        save_write();
        respawn();
        g_mode = 1;
    }
    if (hit_btn(280, 272, 220, 28))
        g_mode = 3;
    if (hit_btn(280, 308, 220, 28))
        g_mode = 0;
}

void ui_inv() {
    int i;
    fillrgb(180, 80, 440, 360, 0x00F3EEE4);
    text(200, 96, "Inventario", 0x001C1814);
    i = 0;
    while (i < 8) {
        num(g_inv[i]);
        text(200, 140 + i * 24, g_num, 0x001C1814);
        i = i + 1;
    }
    num(g_arrows);
    text(360, 140, g_num, 0x001C1814);
    text(400, 140, "flechas", 0x001C1814);
    num(g_wood);
    text(360, 164, g_num, 0x001C1814);
    text(400, 164, "madeira", 0x001C1814);
    num(g_pearl);
    text(360, 188, g_num, 0x001C1814);
    text(400, 188, "perolas", 0x001C1814);
}

void ui_hud() {
    int x;
    int i;
    int hour;
    hour = clock_h();
    num(hour);
    text(16, 16, g_num, 0x00F3EEE4);
    text(48, 16, "h", 0x00F3EEE4);
    num(g_phase);
    text(80, 16, g_num, 0x00C4842A);
    if (g_rain)
        text(120, 16, "chuva", 0x00F3EEE4);
    if (g_toast > 0 && g_note == 1)
        text(16, 40, "salvo", 0x00F3EEE4);
    if (g_toast > 0 && g_note == 2)
        text(16, 40, "carregado", 0x00F3EEE4);
    if (g_toast > 0 && g_note == 3)
        text(16, 40, "checkpoint", 0x00F3EEE4);
    if (g_toast > 0 && g_note == 4)
        text(16, 40, "coletado", 0x00F3EEE4);
    if (g_toast > 0 && g_note == 5)
        text(16, 40, "sem flechas", 0x00F3EEE4);
    if (g_toast > 0 && g_note == 6)
        text(16, 40, "sem blocos", 0x00F3EEE4);
    if (g_toast > 0 && g_note == 7)
        text(16, 40, "fase mar", 0x00F3EEE4);
    if (g_toast > 0 && g_note == 8)
        text(16, 40, "fase torre", 0x00F3EEE4);
    if (g_toast > 0)
        g_toast = g_toast - 1;
    line(392, 300, 408, 300, 15);
    line(400, 292, 400, 308, 15);
    pixel(400, 300, 15);
    x = 250;
    i = 0;
    while (i < 5) {
        fillrgb(x, 540, 36, 28, 0x00221C18);
        if (i == g_hand)
            fillrgb(x, 568, 36, 4, 0x00C4842A);
        x = x + 40;
        i = i + 1;
    }
    num(fps());
    text(720, 16, g_num, 0x00F3EEE4);
    if (g_rain) {
        i = 0;
        while (i < 24) {
            x = (i * 34 + (ticks() % 40)) % 800;
            line(x, 40 + i * 8, x + 2, 64 + i * 8, 1);
            i = i + 1;
        }
    }
    if (g_mode == 5) {
        fillrgb(200, 220, 400, 80, 0x00221C18);
        text(250, 248, "Torre concluida", 0x00F3EEE4);
    }
}
