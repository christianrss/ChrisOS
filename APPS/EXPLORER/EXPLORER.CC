char g_cwd[96];
char g_ent[64];
char g_path[160];
char g_show[64];
int g_ntotal;
int g_nshow;
int g_prev;
int g_scroll;
int g_hscroll;
int g_empty;
int g_sel;

void cwd_set(int s) {
  int i;
  i = 0;
  while (loadb(s + i) != 0) {
    storeb(g_cwd + i, loadb(s + i));
    i = i + 1;
  }
  storeb(g_cwd + i, 0);
  g_scroll = 0;
  g_hscroll = 0;
  g_sel = 0;
}

void cwd_init() {
  storeb(g_cwd, 0);
}

int name_len(int s) {
  int n;
  n = 0;
  while (loadb(s + n) != 0) {
    n = n + 1;
  }
  return n;
}

int name_ok(int s) {
  int i;
  int c;
  i = 0;
  if (loadb(s) == 0) {
    return 0;
  }
  while (loadb(s + i) != 0) {
    c = loadb(s + i);
    if (c < 32) {
      return 0;
    }
    if (c > 126) {
      return 0;
    }
    i = i + 1;
  }
  return 1;
}

void copy_show(int src, int off) {
  int i;
  int j;
  int n;
  n = name_len(src);
  if (off < 0) {
    off = 0;
  }
  if (off > n) {
    off = n;
  }
  i = off;
  j = 0;
  while (loadb(src + i) != 0) {
    if (j < 46) {
      storeb(g_show + j, loadb(src + i));
      j = j + 1;
    }
    i = i + 1;
  }
  storeb(g_show + j, 0);
}

int is_clv(int s) {
  int n;
  n = name_len(s);
  if (n < 4) {
    return 0;
  }
  if (loadb(s + n - 4) != 46) {
    return 0;
  }
  if (loadb(s + n - 1) == 86) {
    return 1;
  }
  if (loadb(s + n - 1) == 118) {
    return 1;
  }
  return 0;
}

int is_textish(int s) {
  int n;
  n = name_len(s);
  if (n < 3) {
    return 0;
  }
  if (loadb(s + n - 3) == 46) {
    if (loadb(s + n - 2) == 67) {
      if (loadb(s + n - 1) == 67) {
        return 1;
      }
    }
    if (loadb(s + n - 2) == 99) {
      if (loadb(s + n - 1) == 99) {
        return 1;
      }
    }
  }
  if (n >= 4) {
    if (loadb(s + n - 4) == 46) {
      if (loadb(s + n - 3) == 84) {
        if (loadb(s + n - 2) == 88) {
          return 1;
        }
      }
      if (loadb(s + n - 3) == 116) {
        if (loadb(s + n - 2) == 120) {
          return 1;
        }
      }
    }
  }
  return 0;
}

void join_path(int dst, int dir, int name) {
  int i;
  int j;
  i = 0;
  while (loadb(dir + i) != 0) {
    storeb(dst + i, loadb(dir + i));
    i = i + 1;
  }
  if (i > 0) {
    storeb(dst + i, 47);
    i = i + 1;
  }
  j = 0;
  while (loadb(name + j) != 0) {
    storeb(dst + i, loadb(name + j));
    i = i + 1;
    j = j + 1;
  }
  storeb(dst + i, 0);
}

void parent_cwd() {
  int n;
  int i;
  int slash;
  n = name_len(g_cwd);
  if (n <= 0) {
    return;
  }
  slash = -1;
  i = 0;
  while (i < n) {
    if (loadb(g_cwd + i) == 47) {
      slash = i;
    }
    i = i + 1;
  }
  if (slash < 0) {
    storeb(g_cwd, 0);
  } else {
    storeb(g_cwd + slash, 0);
  }
  g_scroll = 0;
  g_hscroll = 0;
  g_sel = 0;
}

void open_entry(int name) {
  join_path(g_path, g_cwd, name);
  if (is_clv(name)) {
    app_launch(g_path);
    return;
  }
  if (is_textish(name)) {
    app_spawn_arg("APPS/EDITOR/EDITOR.CLV", g_path);
    return;
  }
  if (isdir(g_path)) {
    cwd_set(g_path);
    return;
  }
}

int count_entries() {
  int i;
  int got;
  int n;
  n = 0;
  i = 0;
  while (i < 512) {
    got = readdir(g_cwd, i, g_ent);
    if (got <= 0) {
      return n;
    }
    if (name_ok(g_ent)) {
      n = n + 1;
    }
    i = i + 1;
  }
  return n;
}

int entry_at(int want) {
  int i;
  int got;
  int n;
  n = 0;
  i = 0;
  while (i < 512) {
    got = readdir(g_cwd, i, g_ent);
    if (got <= 0) {
      return 0;
    }
    if (name_ok(g_ent)) {
      if (n == want) {
        return 1;
      }
      n = n + 1;
    }
    i = i + 1;
  }
  return 0;
}

void main() {
  int live;
  int i;
  int y;
  int mb;
  int my;
  int mx;
  int row;
  int view;
  int max_off;
  int list_h;
  int list_w;
  int hoff;
  int k;
  win_origin(40, 36);
  cwd_init();
  g_nshow = 0;
  g_prev = 0;
  g_scroll = 0;
  g_hscroll = 0;
  g_sel = 0;
  while (1) {
    live = win_begin(520, 380, "Files");
    if (live == 0) {
      return;
    }
    g_ntotal = count_entries();
    if (g_sel < 0) {
      g_sel = 0;
    }
    if (g_sel >= g_ntotal) {
      if (g_ntotal > 0) {
        g_sel = g_ntotal - 1;
      } else {
        g_sel = 0;
      }
    }
    list_w = win_w() - 28;
    list_h = win_h() - 68 - 18;
    view = list_h / 18;
    if (view < 1) {
      view = 1;
    }
    if (g_sel < g_scroll) {
      g_scroll = g_sel;
    }
    if (g_sel >= g_scroll + view) {
      g_scroll = g_sel - view + 1;
    }
    max_off = g_ntotal - view;
    if (max_off < 0) {
      max_off = 0;
    }
    g_scroll = win_vscroll(win_w() - 18, 68, list_h, max_off, view, g_scroll);
    hoff = 0;
    if (loadb(g_cwd) != 0) {
      hoff = name_len(g_cwd);
      if (hoff > 20) {
        hoff = hoff - 20;
      } else {
        hoff = 0;
      }
    }
    g_hscroll = win_hscroll(6, win_h() - 16, list_w, hoff, 20, g_hscroll);
    if (loadb(g_cwd) == 0) {
      text(8, 24, "/", 0x00603818);
    } else {
      copy_show(g_cwd, g_hscroll);
      text(8, 24, g_show, 0x00603818);
    }
    text(8, 44, ".. parent   Enter opens   click file", 0x00504038);
    y = 68;
    g_nshow = 0;
    g_empty = 1;
    fillrgb(6, y, list_w - 4, 16, 0x00304050);
    text(10, y + 1, "..", 0x00FFFFFF);
    g_nshow = 1;
    y = y + 18;
    i = g_scroll;
    while (i < g_ntotal) {
      if (g_nshow > view) {
        i = g_ntotal;
      } else {
        if (entry_at(i)) {
          g_empty = 0;
          if (i == g_sel) {
            fillrgb(6, y, list_w - 4, 16, 0x00406080);
          } else {
            fillrgb(6, y, list_w - 4, 16, 0x00203040);
          }
          text(10, y + 1, g_ent, 0x00FFFFFF);
          g_nshow = g_nshow + 1;
          y = y + 18;
        }
        i = i + 1;
      }
    }
    if (g_empty) {
      text(10, y + 4, "(empty dir)", 0x00504038);
    }
    k = ev_key();
    while (k) {
      if (k == 7) {
        if (g_sel > 0) {
          g_sel = g_sel - 1;
        }
      }
      if (k == 8) {
        if (g_sel + 1 < g_ntotal) {
          g_sel = g_sel + 1;
        }
      }
      if (k == 3) {
        if (entry_at(g_sel)) {
          open_entry(g_ent);
        }
      }
      k = ev_key();
    }
    mb = mouse_btn();
    if (mb) {
      if (g_prev == 0) {
        if (win_mouse_in()) {
          mx = mouse_x() - win_ox();
          my = mouse_y() - win_oy();
          if (mx >= 6) {
            if (mx < list_w) {
              if (my >= 68) {
                if (my < 68 + list_h) {
                  row = (my - 68) / 18;
                  if (row == 0) {
                    parent_cwd();
                  } else {
                    if (row > 0) {
                      if (row < g_nshow) {
                        i = g_scroll + row - 1;
                        if (i >= 0) {
                          if (i < g_ntotal) {
                            g_sel = i;
                            if (entry_at(i)) {
                              open_entry(g_ent);
                            }
                          }
                        }
                      }
                    }
                  }
                }
              }
            }
          }
        }
      }
    }
    g_prev = mb;
    win_end();
    wait(1);
  }
}
