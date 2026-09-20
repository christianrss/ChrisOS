char g_cwd[96];
char g_ent[64];
char g_path[160];
int g_nshow;
int g_prev;

void cwd_init() {
  storeb(g_cwd, 65);
  storeb(g_cwd + 1, 80);
  storeb(g_cwd + 2, 80);
  storeb(g_cwd + 3, 83);
  storeb(g_cwd + 4, 0);
}

int name_len(int s) {
  int n;
  n = 0;
  while (loadb(s + n) != 0) {
    n = n + 1;
  }
  return n;
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
  if (loadb(s + n - 3) != 67) {
    if (loadb(s + n - 3) != 99) {
      return 0;
    }
  }
  if (loadb(s + n - 2) != 76) {
    if (loadb(s + n - 2) != 108) {
      return 0;
    }
  }
  if (loadb(s + n - 1) != 86) {
    if (loadb(s + n - 1) != 118) {
      return 0;
    }
  }
  return 1;
}

void join_path(int dst, int dir, int name) {
  int i;
  int j;
  i = 0;
  while (loadb(dir + i) != 0) {
    storeb(dst + i, loadb(dir + i));
    i = i + 1;
  }
  storeb(dst + i, 47);
  i = i + 1;
  j = 0;
  while (loadb(name + j) != 0) {
    storeb(dst + i, loadb(name + j));
    i = i + 1;
    j = j + 1;
  }
  storeb(dst + i, 0);
}

void main() {
  int live;
  int i;
  int y;
  int got;
  int mb;
  int my;
  int row;
  cwd_init();
  g_nshow = 0;
  g_prev = 0;
  while (1) {
    live = win_begin(480, 340, "Files");
    if (live == 0) {
      return;
    }
    text(8, 24, g_cwd, 0x00A0E0FF);
    i = 0;
    y = 44;
    g_nshow = 0;
    while (i < 14) {
      got = readdir(g_cwd, i, g_ent);
      if (got == 0) {
        i = 14;
      } else {
        if (got > 0) {
          fillrgb(6, y, 468, 16, 0x00203040);
          text(10, y + 1, g_ent, 0x00FFFFFF);
          g_nshow = g_nshow + 1;
          y = y + 18;
        }
        i = i + 1;
      }
    }
    mb = mouse_btn();
    if (mb) {
      if (g_prev == 0) {
        my = mouse_y() - win_oy();
        if (my >= 44) {
          row = (my - 44) / 18;
          if (row >= 0) {
            if (row < g_nshow) {
              got = readdir(g_cwd, row, g_ent);
              if (got > 0) {
                if (is_clv(g_ent)) {
                  join_path(g_path, g_cwd, g_ent);
                  app_launch(g_path);
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
