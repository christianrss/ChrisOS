char g_line[128];
int g_n;
char g_out[160];
int g_status;

int starts4(int s, int a, int b, int c, int d) {
  if (loadb(s) != a) {
    return 0;
  }
  if (loadb(s + 1) != b) {
    return 0;
  }
  if (loadb(s + 2) != c) {
    return 0;
  }
  if (loadb(s + 3) != d) {
    return 0;
  }
  return 1;
}

int starts3(int s, int a, int b, int c) {
  if (loadb(s) != a) {
    return 0;
  }
  if (loadb(s + 1) != b) {
    return 0;
  }
  if (loadb(s + 2) != c) {
    return 0;
  }
  return 1;
}

int starts2(int s, int a, int b) {
  if (loadb(s) != a) {
    return 0;
  }
  if (loadb(s + 1) != b) {
    return 0;
  }
  return 1;
}

void set_out(int msg) {
  int i;
  i = 0;
  while (loadb(msg + i) != 0) {
    storeb(g_out + i, loadb(msg + i));
    i = i + 1;
  }
  storeb(g_out + i, 0);
}

int starts5(int s) {
  if (starts4(s, 109, 97, 107, 101) == 0) {
    return 0;
  }
  if (loadb(s + 4) == 0) {
    g_status = sys_make("Makefile", "all");
  } else {
    if (loadb(s + 4) == 32) {
      g_status = sys_make(s + 5, "all");
    } else {
      return 0;
    }
  }
  if (g_status) {
    set_out("make ok");
  } else {
    set_out("make fail");
  }
  return 1;
}

void run_line() {
  g_status = 0;
  if (g_n < 1) {
    return;
  }
  if (starts2(g_line, 108, 115)) {
    set_out("ls: use Files");
    return;
  }
  if (starts4(g_line, 104, 101, 108, 112)) {
    set_out("run cc make");
    return;
  }
  if (starts4(g_line, 114, 117, 110, 32)) {
    g_status = sys_run(g_line + 4);
    if (g_status) {
      set_out("run ok");
    } else {
      set_out("run fail");
    }
    return;
  }
  if (starts3(g_line, 99, 99, 32)) {
    g_status = sys_cc(g_line + 3);
    if (g_status) {
      set_out("cc ok");
    } else {
      set_out("cc fail");
    }
    return;
  }
  if (starts5(g_line)) {
    return;
  }
  set_out("?");
}

void main() {
  int k;
  int ch;
  int live;
  g_n = 0;
  storeb(g_line, 0);
  set_out("help run cc make");
  while (1) {
    live = win_begin(520, 300, "Shell");
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
        if (g_n > 0) {
          g_n = g_n - 1;
          storeb(g_line + g_n, 0);
        }
      }
      if (k == 3) {
        run_line();
        g_n = 0;
        storeb(g_line, 0);
      }
      k = ev_key();
    }
    ch = ev_text();
    while (ch) {
      if (g_n < 126) {
        storeb(g_line + g_n, ch);
        g_n = g_n + 1;
        storeb(g_line + g_n, 0);
      }
      ch = ev_text();
    }
    text(8, 28, ">", 0x00B0FFB0);
    text(20, 28, g_line, 0x00FFFFFF);
    text(8, 52, g_out, 0x00C0D8F0);
    text(8, 76, "run PATH  cc PATH  make FILE", 0x0080A0B0);
    win_end();
    wait(1);
  }
}
