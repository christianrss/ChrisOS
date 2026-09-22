#pragma abi(1, 0)

int g_win_x;
int g_win_y;
int g_win_w;
int g_win_h;
int g_win_drag;
int g_win_ox;
int g_win_oy;
int g_win_prev;
int g_win_edge;
int g_win_ready;
int g_win_placed;
int g_bar_h;

void win_origin(int x, int y) {
  if (g_win_ready == 0) {
    g_win_x = x;
    g_win_y = y;
    g_win_placed = 1;
  }
}

int win_mouse_in() {
  int mx;
  int my;
  mx = mouse_x();
  my = mouse_y();
  if (mx < g_win_x) {
    return 0;
  }
  if (my < g_win_y) {
    return 0;
  }
  if (mx >= g_win_x + g_win_w) {
    return 0;
  }
  if (my >= g_win_y + g_win_h) {
    return 0;
  }
  return 1;
}

int win_begin(int w, int h, int title) {
  int mx;
  int my;
  int mb;
  int dw;
  int dh;
  int was;
  dw = disp_w();
  dh = disp_h();
  g_bar_h = 40;
  if (g_win_ready == 0) {
    g_win_w = w;
    g_win_h = h;
    if (g_win_placed == 0) {
      g_win_x = 56;
      g_win_y = 48;
    }
    if (g_win_x + g_win_w > dw) {
      g_win_x = 8;
    }
    if (g_win_y + g_win_h > dh - g_bar_h) {
      g_win_y = 8;
    }
    viewport(g_win_w, g_win_h);
    surf_place(g_win_x, g_win_y, g_win_w, g_win_h);
    g_win_ready = 1;
  }
  mx = mouse_x();
  my = mouse_y();
  mb = mouse_btn();
  was = g_win_prev;
  g_win_edge = 0;
  if (mb) {
    if (was == 0) {
      g_win_edge = 1;
    }
  }
  if (g_win_drag) {
    if (mb == 0) {
      g_win_drag = 0;
    } else {
      g_win_x = mx - g_win_ox;
      g_win_y = my - g_win_oy;
      if (g_win_y < 0) {
        g_win_y = 0;
      }
      if (g_win_y + g_win_h > dh - g_bar_h) {
        g_win_y = dh - g_bar_h - g_win_h;
        if (g_win_y < 0) {
          g_win_y = 0;
        }
      }
      if (g_win_x < 0) {
        g_win_x = 0;
      }
      if (g_win_x + g_win_w > dw) {
        g_win_x = dw - g_win_w;
        if (g_win_x < 0) {
          g_win_x = 0;
        }
      }
      surf_move(g_win_x, g_win_y);
    }
  } else {
    if (g_win_edge) {
      if (mx >= g_win_x + g_win_w - 22) {
        if (mx < g_win_x + g_win_w) {
          if (my >= g_win_y) {
            if (my < g_win_y + 20) {
              g_win_prev = mb;
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
  if (g_win_edge == 0) {
    return 0;
  }
  if (win_mouse_in() == 0) {
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

int win_w() {
  return g_win_w;
}

int win_h() {
  return g_win_h;
}

int win_edge() {
  return g_win_edge;
}

int win_vscroll(int x, int y, int h, int max_off, int view, int scroll) {
  int track;
  int thumb;
  int ty;
  int th;
  int mx;
  int my;
  int ns;
  if (h < 40) {
    return scroll;
  }
  if (max_off < 0) {
    max_off = 0;
  }
  if (scroll < 0) {
    scroll = 0;
  }
  if (scroll > max_off) {
    scroll = max_off;
  }
  track = h - 32;
  fillrgb(x, y, 14, h, 0x00202830);
  fillrgb(x + 1, y + 1, 12, 14, 0x00406080);
  text(x + 3, y + 1, "^", 0x00FFFFFF);
  fillrgb(x + 1, y + h - 15, 12, 14, 0x00406080);
  text(x + 3, y + h - 14, "v", 0x00FFFFFF);
  if (max_off <= 0) {
    fillrgb(x + 2, y + 16, 10, track, 0x00304050);
  } else {
    th = (track * view) / (view + max_off);
    if (th < 12) {
      th = 12;
    }
    if (th > track) {
      th = track;
    }
    ty = y + 16 + ((track - th) * scroll) / max_off;
    fillrgb(x + 2, y + 16, 10, track, 0x00304050);
    fillrgb(x + 2, ty, 10, th, 0x007090B0);
  }
  if (g_win_edge) {
    if (win_mouse_in()) {
      mx = mouse_x() - g_win_x;
      my = mouse_y() - g_win_y;
      if (mx >= x) {
        if (mx < x + 14) {
          if (my >= y) {
            if (my < y + 16) {
              ns = scroll - 1;
              if (ns < 0) {
                ns = 0;
              }
              return ns;
            }
          }
          if (my >= y + h - 16) {
            if (my < y + h) {
              ns = scroll + 1;
              if (ns > max_off) {
                ns = max_off;
              }
              return ns;
            }
          }
          if (my >= y + 16) {
            if (my < y + h - 16) {
              if (max_off > 0) {
                ns = ((my - (y + 16)) * max_off) / track;
                if (ns < 0) {
                  ns = 0;
                }
                if (ns > max_off) {
                  ns = max_off;
                }
                return ns;
              }
            }
          }
        }
      }
    }
  }
  return scroll;
}

int win_hscroll(int x, int y, int w, int max_off, int view, int scroll) {
  int track;
  int thumb;
  int tx;
  int tw;
  int mx;
  int my;
  int ns;
  if (w < 40) {
    return scroll;
  }
  if (max_off < 0) {
    max_off = 0;
  }
  if (scroll < 0) {
    scroll = 0;
  }
  if (scroll > max_off) {
    scroll = max_off;
  }
  track = w - 32;
  fillrgb(x, y, w, 14, 0x00202830);
  fillrgb(x + 1, y + 1, 14, 12, 0x00406080);
  text(x + 3, y + 1, "<", 0x00FFFFFF);
  fillrgb(x + w - 15, y + 1, 14, 12, 0x00406080);
  text(x + w - 12, y + 1, ">", 0x00FFFFFF);
  if (max_off <= 0) {
    fillrgb(x + 16, y + 2, track, 10, 0x00304050);
  } else {
    tw = (track * view) / (view + max_off);
    if (tw < 12) {
      tw = 12;
    }
    if (tw > track) {
      tw = track;
    }
    tx = x + 16 + ((track - tw) * scroll) / max_off;
    fillrgb(x + 16, y + 2, track, 10, 0x00304050);
    fillrgb(tx, y + 2, tw, 10, 0x007090B0);
  }
  if (g_win_edge) {
    if (win_mouse_in()) {
      mx = mouse_x() - g_win_x;
      my = mouse_y() - g_win_y;
      if (my >= y) {
        if (my < y + 14) {
          if (mx >= x) {
            if (mx < x + 16) {
              ns = scroll - 4;
              if (ns < 0) {
                ns = 0;
              }
              return ns;
            }
          }
          if (mx >= x + w - 16) {
            if (mx < x + w) {
              ns = scroll + 4;
              if (ns > max_off) {
                ns = max_off;
              }
              return ns;
            }
          }
          if (mx >= x + 16) {
            if (mx < x + w - 16) {
              if (max_off > 0) {
                ns = ((mx - (x + 16)) * max_off) / track;
                if (ns < 0) {
                  ns = 0;
                }
                if (ns > max_off) {
                  ns = max_off;
                }
                return ns;
              }
            }
          }
        }
      }
    }
  }
  return scroll;
}
