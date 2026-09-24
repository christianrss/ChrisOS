int clock_h() {
    int t;
    t = ticks() + g_bias;
    if (t < 0)
        t = 0;
    t = (t / 40) % 24;
    return t;
}

void sky_frame() {
    int hour;
    float _Complex sun;
    float wave;
    int day;
    hour = clock_h();
    day = hour >= 6 && hour < 18;
    g_rain = hour >= 16 && hour < 19;
    sun.re = cos((float)hour * 15.0);
    sun.im = sin((float)hour * 15.0);
    clear(0);
    if (day) {
        light(sun.re * 8.0, 12.0 + sun.im, 4.0, 1.0, 0.95, 0.8);
        if (g_rain)
            fillrgb(0, 0, 800, 600, 0x00586870);
        else
            fillrgb(0, 0, 800, 600, 0x007BA3D4);
    } else {
        light(sun.re * 4.0, 3.0, -6.0, 0.25, 0.3, 0.55);
        fillrgb(0, 0, 800, 600, 0x00101828);
    }
    wave = (float)ticks();
    wave = wave % 100.0;
    tex_ofs(wave / 100.0, 0.04);
    draw_mob(70.0 + sun.re * 6.0, 18.0 + sun.im * 4.0, 70.0, 0.0, 14);
}

void sky_flip() {
    if (clock_h() >= 6 && clock_h() < 18)
        g_bias = g_bias + 400;
    else
        g_bias = g_bias - 400;
}
