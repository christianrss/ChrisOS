int g_x;
int g_y;
int g_vx;
int g_vy;
int g_ready;

void main() {
  int live;
  int bw;
  int bh;
  g_ready = 0;
  while (1) {
    live = win_begin(360, 280, "Ball");
    if (live == 0) {
      return;
    }
    bw = 360;
    bh = 280;
    if (g_ready == 0) {
      g_x = 40;
      g_y = 60;
      g_vx = 3;
      g_vy = 2;
      g_ready = 1;
    }
    g_x = g_x + g_vx;
    g_y = g_y + g_vy;
    if (g_x < 4) {
      g_x = 4;
      g_vx = 0 - g_vx;
    }
    if (g_x > bw - 24) {
      g_x = bw - 24;
      g_vx = 0 - g_vx;
    }
    if (g_y < 24) {
      g_y = 24;
      g_vy = 0 - g_vy;
    }
    if (g_y > bh - 24) {
      g_y = bh - 24;
      g_vy = 0 - g_vy;
    }
    fillrgb(g_x, g_y, 20, 20, 0x0000C060);
    win_end();
    wait(1);
  }
}
