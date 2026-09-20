int g_bar_prev;

int bar_hit(int mx, int x0, int x1) {
  if (mx >= x0) {
    if (mx < x1) {
      return 1;
    }
  }
  return 0;
}

void main() {
  int dw;
  int dh;
  int mx;
  int my;
  int mb;
  int sec;
  int m;
  int s;
  char clk[6];
  dw = disp_w();
  dh = disp_h();
  if (dw < 1) {
    dw = 640;
  }
  if (dh < 40) {
    dh = 400;
  }
  viewport(640, 32);
  surf_place(0, dh - 32, dw, 32);
  g_bar_prev = 0;
  while (1) {
    surf_raise();
    surf_place(0, dh - 32, dw, 32);
    mb = mouse_btn();
    mx = mouse_x() * 640 / dw;
    my = mouse_y();
    fillrgb(0, 0, 640, 32, 0x00203040);
    fillrgb(4, 6, 56, 20, 0x00007090);
    text(10, 8, "Shell", 0x00FFFFFF);
    fillrgb(64, 6, 56, 20, 0x00008070);
    text(70, 8, "Files", 0x00FFFFFF);
    fillrgb(124, 6, 56, 20, 0x00804020);
    text(134, 8, "Edit", 0x00FFFFFF);
    fillrgb(184, 6, 56, 20, 0x00604080);
    text(190, 8, "Tasks", 0x00FFFFFF);
    fillrgb(244, 6, 56, 20, 0x00008040);
    text(256, 8, "Ball", 0x00FFFFFF);
    sec = ticks() / 60;
    m = (sec / 60) % 60;
    s = sec % 60;
    clk[0] = 48 + (m / 10);
    clk[1] = 48 + (m % 10);
    clk[2] = 58;
    clk[3] = 48 + (s / 10);
    clk[4] = 48 + (s % 10);
    clk[5] = 0;
    text(580, 8, clk, 0x00E0F0FF);
    if (mb) {
      if (g_bar_prev == 0) {
        if (my >= dh - 32) {
          if (bar_hit(mx, 4, 60)) {
            app_launch("APPS/SHELL/SHELL.CLV");
          }
          if (bar_hit(mx, 64, 120)) {
            app_launch("APPS/EXPLORER/EXPLORER.CLV");
          }
          if (bar_hit(mx, 124, 180)) {
            app_launch("APPS/EDITOR/EDITOR.CLV");
          }
          if (bar_hit(mx, 184, 240)) {
            app_launch("APPS/TASKMGR/TASKMGR.CLV");
          }
          if (bar_hit(mx, 244, 300)) {
            app_launch("APPS/BALL/BALL.CLV");
          }
        }
      }
    }
    g_bar_prev = mb;
    wait(1);
  }
}
