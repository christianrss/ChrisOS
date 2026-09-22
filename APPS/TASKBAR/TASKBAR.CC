int g_bar_prev;

void main() {
  int dw;
  int dh;
  int mx;
  int my;
  int mb;
  int h;
  int sec;
  int m;
  int s;
  char clk[6];
  dw = disp_w();
  dh = disp_h();
  if (dw < 320) {
    dw = 640;
  }
  if (dh < 80) {
    dh = 400;
  }
  h = 40;
  viewport(dw, h);
  surf_place(0, dh - h, dw, h);
  g_bar_prev = 0;
  while (1) {
    mb = mouse_btn();
    mx = mouse_x();
    my = mouse_y();
    fillrgb(0, 0, dw, h, 0x00203040);
    text(8, 12, "ChrisOS", 0x00E0F0FF);
    fillrgb(80, 8, 64, 24, 0x006090);
    text(90, 12, "Shell", 0x00FFFFFF);
    fillrgb(152, 8, 64, 24, 0x008070);
    text(162, 12, "Files", 0x00FFFFFF);
    fillrgb(224, 8, 64, 24, 0x804020);
    text(238, 12, "Edit", 0x00FFFFFF);
    fillrgb(296, 8, 64, 24, 0x604080);
    text(306, 12, "Tasks", 0x00FFFFFF);
    fillrgb(368, 8, 64, 24, 0x008040);
    text(382, 12, "Ball", 0x00FFFFFF);
    fillrgb(440, 8, 64, 24, 0x802010);
    text(452, 12, "Doom", 0x00FFFFFF);
    fillrgb(512, 8, 64, 24, 0x406080);
    text(522, 12, "Prefs", 0x00FFFFFF);
    sec = ticks() / 60;
    m = (sec / 60) % 60;
    s = sec % 60;
    clk[0] = 48 + (m / 10);
    clk[1] = 48 + (m % 10);
    clk[2] = 58;
    clk[3] = 48 + (s / 10);
    clk[4] = 48 + (s % 10);
    clk[5] = 0;
    text(dw - 56, 12, clk, 0x00E0F0FF);
    if (mb) {
      if (g_bar_prev == 0) {
        if (my >= dh - h) {
          if (mx >= 80) {
            if (mx < 144) {
              app_launch("APPS/SHELL/SHELL.CLV");
            }
          }
          if (mx >= 152) {
            if (mx < 216) {
              app_launch("APPS/EXPLORER/EXPLORER.CLV");
            }
          }
          if (mx >= 224) {
            if (mx < 288) {
              app_launch("APPS/EDITOR/EDITOR.CLV");
            }
          }
          if (mx >= 296) {
            if (mx < 360) {
              app_launch("APPS/TASKMGR/TASKMGR.CLV");
            }
          }
          if (mx >= 368) {
            if (mx < 432) {
              app_launch("APPS/BALL/BALL.CLV");
            }
          }
          if (mx >= 440) {
            if (mx < 504) {
              app_launch("GAMES/DOOM/ENGINE.CLV");
            }
          }
          if (mx >= 512) {
            if (mx < 576) {
              app_launch("APPS/PREFS/PREFS.CLV");
            }
          }
        }
      }
    }
    g_bar_prev = mb;
    wait(1);
  }
}
