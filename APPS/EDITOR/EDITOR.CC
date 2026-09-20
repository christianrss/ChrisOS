char g_buf[1024];
int g_len;
char g_path[32];
int g_msg;

void path_init() {
  storeb(g_path, 83);
  storeb(g_path + 1, 82);
  storeb(g_path + 2, 67);
  storeb(g_path + 3, 47);
  storeb(g_path + 4, 78);
  storeb(g_path + 5, 79);
  storeb(g_path + 6, 84);
  storeb(g_path + 7, 69);
  storeb(g_path + 8, 46);
  storeb(g_path + 9, 84);
  storeb(g_path + 10, 88);
  storeb(g_path + 11, 84);
  storeb(g_path + 12, 0);
}

void main() {
  int live;
  int k;
  int ch;
  int fd;
  int n;
  path_init();
  g_len = 0;
  storeb(g_buf, 0);
  g_msg = 0;
  fd = fopen("SRC/NOTE.TXT");
  if (fd >= 0) {
    n = fread(fd, g_buf, 1023);
    fclose(fd);
    if (n < 0) {
      n = 0;
    }
    if (n > 1023) {
      n = 1023;
    }
    g_len = n;
    storeb(g_buf + g_len, 0);
  }
  while (1) {
    live = win_begin(560, 360, "Editor");
    if (live == 0) {
      return;
    }
    k = ev_key();
    while (k) {
      if (k == 4) {
        surf_close();
        return;
      }
      if (k == 1) {
        if (g_len > 0) {
          g_len = g_len - 1;
          storeb(g_buf + g_len, 0);
        }
      }
      if (k == 3) {
        if (g_len < 1022) {
          storeb(g_buf + g_len, 10);
          g_len = g_len + 1;
          storeb(g_buf + g_len, 0);
        }
      }
      if (k == 12) {
        fd = fopen("SRC/NOTE.TXT");
        if (fd < 0) {
          g_msg = 2;
        } else {
          fwrite(fd, g_buf, g_len);
          fclose(fd);
          g_msg = 1;
        }
      }
      k = ev_key();
    }
    ch = ev_text();
    while (ch) {
      if (g_len < 1022) {
        storeb(g_buf + g_len, ch);
        g_len = g_len + 1;
        storeb(g_buf + g_len, 0);
      }
      ch = ev_text();
    }
    text(8, 24, "F2 save  Esc close", 0x0080A0B0);
    text(8, 44, g_buf, 0x00FFFFFF);
    if (g_msg == 1) {
      text(8, 330, "saved SRC/NOTE.TXT", 0x0000FF80);
    }
    if (g_msg == 2) {
      text(8, 330, "save fail", 0x00FF6060);
    }
    win_end();
    wait(1);
  }
}
