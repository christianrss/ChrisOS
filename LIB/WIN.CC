int g_win_x;
int g_win_y;
int g_win_w;
int g_win_h;
int g_win_drag;
int g_win_ox;
int g_win_oy;
int g_win_prev;
int g_win_ready;

int win_begin(int w, int h, int title) {
  int mx;
  int my;
  int mb;
  int dw;
  int dh;
  dw = disp_w();
  dh = disp_h();
  if (g_win_ready == 0) {
    g_win_w = w;
    g_win_h = h;
    g_win_x = 56;
    g_win_y = 48;
    if (g_win_x + g_win_w > dw) {
      g_win_x = 8;
    }
    if (g_win_y + g_win_h > dh) {
      g_win_y = 8;
    }
    viewport(g_win_w, g_win_h);
    surf_place(g_win_x, g_win_y, g_win_w, g_win_h);
    g_win_ready = 1;
  }
  mx = mouse_x();
  my = mouse_y();
  mb = mouse_btn();
  if (g_win_drag) {
    if (mb == 0) {
      g_win_drag = 0;
    } else {
      g_win_x = mx - g_win_ox;
      g_win_y = my - g_win_oy;
      if (g_win_y < 0) {
        g_win_y = 0;
      }
      surf_move(g_win_x, g_win_y);
    }
  } else {
    if (mb) {
      if (g_win_prev == 0) {
        if (mx >= g_win_x + g_win_w - 22) {
          if (mx < g_win_x + g_win_w) {
            if (my >= g_win_y) {
              if (my < g_win_y + 20) {
                surf_close();
                return 0;
              }
            }
          }
        }
        if (mx >= g_win_x) {
          if (mx < g_win_x + g_win_w - 22) {
            if (my >= g_win_y) {
              if (my < g_win_y + 20) {
                g_win_drag = 1;
                g_win_ox = mx - g_win_x;
                g_win_oy = my - g_win_y;
                surf_raise();
              }
            }
          }
        }
      }
    }
  }
  g_win_prev = mb;
  fillrgb(0, 0, g_win_w, 20, 0x00306090);
  fillrgb(0, 20, g_win_w, g_win_h - 20, 0x00101828);
  fillrgb(g_win_w - 22, 0, 22, 20, 0x00C02828);
  text(6, 3, title, 0x00FFFFFF);
  text(g_win_w - 15, 3, "X", 0x00FFFFFF);
  return 1;
}

void win_end() {
}

int win_click(int x, int y, int w, int h) {
  int mx;
  int my;
  int mb;
  mb = mouse_btn();
  if (mb == 0) {
    return 0;
  }
  if (g_win_prev) {
    return 0;
  }
  mx = mouse_x() - g_win_x;
  my = mouse_y() - g_win_y;
  if (mx >= x) {
    if (mx < x + w) {
      if (my >= y) {
        if (my < y + h) {
          return 1;
        }
      }
    }
  }
  return 0;
}

int win_ox() {
  return g_win_x;
}

int win_oy() {
  return g_win_y;
}
