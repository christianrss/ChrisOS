int g_desk_prev;

int icon_hit(int mx, int my, int x, int y) {
  if (mx >= x) {
    if (mx < x + 48) {
      if (my >= y) {
        if (my < y + 64) {
          return 1;
        }
      }
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
  dw = disp_w();
  dh = disp_h();
  if (dw < 1) {
    dw = 640;
  }
  if (dh < 1) {
    dh = 400;
  }
  viewport(640, 400);
  surf_place(0, 0, dw, dh);
  g_desk_prev = 0;
  while (1) {
    mb = mouse_btn();
    mx = mouse_x() * 640 / dw;
    my = mouse_y() * 400 / dh;
    fillrgb(0, 0, 640, 400, 0x0014283C);
    fillrgb(20, 24, 40, 40, 0x00006080);
    text(16, 68, "Shell", 0x00E8F0FF);
    fillrgb(90, 24, 40, 40, 0x00008070);
    text(86, 68, "Files", 0x00E8F0FF);
    fillrgb(160, 24, 40, 40, 0x00804020);
    text(154, 68, "Edit", 0x00E8F0FF);
    fillrgb(230, 24, 40, 40, 0x00604080);
    text(222, 68, "Tasks", 0x00E8F0FF);
    fillrgb(300, 24, 40, 40, 0x00008040);
    text(298, 68, "Ball", 0x00E8F0FF);
    text(20, 360, "ChrisOS", 0x00A0C8E0);
    if (mb) {
      if (g_desk_prev == 0) {
        if (icon_hit(mx, my, 20, 24)) {
          app_launch("APPS/SHELL/SHELL.CLV");
        }
        if (icon_hit(mx, my, 90, 24)) {
          app_launch("APPS/EXPLORER/EXPLORER.CLV");
        }
        if (icon_hit(mx, my, 160, 24)) {
          app_launch("APPS/EDITOR/EDITOR.CLV");
        }
        if (icon_hit(mx, my, 230, 24)) {
          app_launch("APPS/TASKMGR/TASKMGR.CLV");
        }
        if (icon_hit(mx, my, 300, 24)) {
          app_launch("APPS/BALL/BALL.CLV");
        }
      }
    }
    g_desk_prev = mb;
    wait(1);
  }
}
