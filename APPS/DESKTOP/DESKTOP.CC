int g_desk_prev;

void main() {
  int dw;
  int dh;
  int mx;
  int my;
  int mb;
  int bar_h;
  int gap;
  int x;
  int y;
  dw = disp_w();
  dh = disp_h();
  if (dw < 320) {
    dw = 640;
  }
  if (dh < 200) {
    dh = 400;
  }
  bar_h = 40;
  gap = 16;
  viewport(dw, dh);
  surf_place(0, 0, dw, dh);
  g_desk_prev = 0;
  while (1) {
    mb = mouse_btn();
    mx = mouse_x();
    my = mouse_y();
    fillrgb(0, 0, dw, dh, 0x0014283C);
    x = gap;
    y = gap;
    fillrgb(x, y, 48, 48, 0x006090);
    text(x, y + 52, "Shell", 0x00E8F0FF);
    y = y + 72;
    fillrgb(x, y, 48, 48, 0x008070);
    text(x, y + 52, "Files", 0x00E8F0FF);
    y = y + 72;
    fillrgb(x, y, 48, 48, 0x804020);
    text(x, y + 52, "Edit", 0x00E8F0FF);
    y = y + 72;
    fillrgb(x, y, 48, 48, 0x604080);
    text(x, y + 52, "Tasks", 0x00E8F0FF);
    y = y + 72;
    fillrgb(x, y, 48, 48, 0x008040);
    text(x, y + 52, "Ball", 0x00E8F0FF);
    y = y + 72;
    fillrgb(x, y, 48, 48, 0x802010);
    text(x, y + 52, "Doom", 0x00E8F0FF);
    y = y + 72;
    fillrgb(x, y, 48, 48, 0x406080);
    text(x, y + 52, "Prefs", 0x00E8F0FF);
    text(gap, dh - bar_h - 20, "ChrisOS", 0x00A0C8E0);
    if (mb) {
      if (g_desk_prev == 0) {
        if (mx >= gap) {
          if (mx < gap + 48) {
            if (my >= gap) {
              if (my < gap + 64) {
                app_launch("APPS/SHELL/SHELL.CLV");
              }
            }
            if (my >= gap + 72) {
              if (my < gap + 136) {
                app_launch("APPS/EXPLORER/EXPLORER.CLV");
              }
            }
            if (my >= gap + 144) {
              if (my < gap + 208) {
                app_launch("APPS/EDITOR/EDITOR.CLV");
              }
            }
            if (my >= gap + 216) {
              if (my < gap + 280) {
                app_launch("APPS/TASKMGR/TASKMGR.CLV");
              }
            }
            if (my >= gap + 288) {
              if (my < gap + 352) {
                app_launch("APPS/BALL/BALL.CLV");
              }
            }
            if (my >= gap + 360) {
              if (my < gap + 424) {
                app_launch("GAMES/DOOM/ENGINE.CLV");
              }
            }
            if (my >= gap + 432) {
              if (my < gap + 496) {
                app_launch("APPS/PREFS/PREFS.CLV");
              }
            }
          }
        }
      }
    }
    g_desk_prev = mb;
    wait(1);
  }
}
