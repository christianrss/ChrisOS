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
  int col;
  int row;
  int i;
  int n;
  int ix;
  int iy;
  int col_w;
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
  col_w = 96;
  viewport(dw, dh);
  surf_place(0, 0, dw, dh);
  g_desk_prev = 0;
  while (1) {
    mb = mouse_btn();
    mx = mouse_x();
    my = mouse_y();
    fillrgb(0, 0, dw, dh, 0x00181014);
    n = 8;
    i = 0;
    col = 0;
    row = 0;
    while (i < n) {
      ix = gap + col * col_w;
      iy = gap + row * 72;
      if (iy + 72 > dh - bar_h - 8) {
        col = col + 1;
        row = 0;
        ix = gap + col * col_w;
        iy = gap;
      }
      x = ix;
      y = iy;
      if (i == 0) {
        fillrgb(x, y, 48, 48, 0x006090);
        text(x, y + 52, "Shell", 0x00E8F0FF);
      }
      if (i == 1) {
        fillrgb(x, y, 48, 48, 0x008070);
        text(x, y + 52, "Files", 0x00E8F0FF);
      }
      if (i == 2) {
        fillrgb(x, y, 48, 48, 0x804020);
        text(x, y + 52, "Edit", 0x00E8F0FF);
      }
      if (i == 3) {
        fillrgb(x, y, 48, 48, 0x604080);
        text(x, y + 52, "Tasks", 0x00E8F0FF);
      }
      if (i == 4) {
        fillrgb(x, y, 48, 48, 0x008040);
        text(x, y + 52, "Ball", 0x00E8F0FF);
      }
      if (i == 5) {
        fillrgb(x, y, 48, 48, 0x802010);
        text(x, y + 52, "Doom", 0x00E8F0FF);
      }
      if (i == 6) {
        fillrgb(x, y, 48, 48, 0x406080);
        text(x, y + 52, "Prefs", 0x00E8F0FF);
      }
      if (i == 7) {
        fillrgb(x, y, 48, 48, 0xC4842A);
        text(x, y + 52, "Mine Chris", 0x00E8F0FF);
      }
      if (mb) {
        if (g_desk_prev == 0) {
          if (mx >= x) {
            if (mx < x + 48) {
              if (my >= y) {
                if (my < y + 64) {
                  if (i == 0) {
                    app_launch("APPS/SHELL/SHELL.CLV");
                  }
                  if (i == 1) {
                    app_launch("APPS/EXPLORER/EXPLORER.CLV");
                  }
                  if (i == 2) {
                    app_launch("APPS/EDITOR/EDITOR.CLV");
                  }
                  if (i == 3) {
                    app_launch("APPS/TASKMGR/TASKMGR.CLV");
                  }
                  if (i == 4) {
                    app_launch("APPS/BALL/BALL.CLV");
                  }
                  if (i == 5) {
                    app_launch("GAMES/DOOM/ENGINE.CLV");
                  }
                  if (i == 6) {
                    app_launch("APPS/PREFS/PREFS.CLV");
                  }
                  if (i == 7) {
                    app_launch("GAMES/MINE/MINE.CLV");
                  }
                }
              }
            }
          }
        }
      }
      row = row + 1;
      i = i + 1;
    }
    text(gap, dh - bar_h - 20, "ChrisOS", 0x00A0C8E0);
    g_desk_prev = mb;
    wait(1);
  }
}
