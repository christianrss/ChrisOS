char g_line[160];
int g_n;
char g_log[2400];
int g_log_len;
char g_cwd[96];
char g_err[160];
char g_tmp[96];

void log_clear() {
  g_log_len = 0;
  storeb(g_log, 0);
}

void log_line(int msg) {
  int i;
  int c;
  if (g_log_len > 2200) {
    log_clear();
  }
  i = 0;
  while (1) {
    c = loadb(msg + i);
    if (c == 0) {
      storeb(g_log + g_log_len, 10);
      g_log_len = g_log_len + 1;
      storeb(g_log + g_log_len, 0);
      return;
    }
    if (c >= 32) {
      if (c <= 126) {
        storeb(g_log + g_log_len, c);
        g_log_len = g_log_len + 1;
      }
    }
    if (c == 10) {
      storeb(g_log + g_log_len, 10);
      g_log_len = g_log_len + 1;
    }
    i = i + 1;
    if (i > 200) {
      storeb(g_log + g_log_len, 10);
      g_log_len = g_log_len + 1;
      storeb(g_log + g_log_len, 0);
      return;
    }
  }
}

int starts_word(int s, int w) {
  int i;
  i = 0;
  while (loadb(w + i) != 0) {
    if (loadb(s + i) != loadb(w + i)) {
      return 0;
    }
    i = i + 1;
  }
  if (loadb(s + i) == 0) {
    return 1;
  }
  if (loadb(s + i) == 32) {
    return 1;
  }
  return 0;
}

void cwd_init() {
  storeb(g_cwd, 0);
}

void set_cwd(int p) {
  int i;
  i = 0;
  while (loadb(p + i) != 0) {
    storeb(g_cwd + i, loadb(p + i));
    i = i + 1;
  }
  storeb(g_cwd + i, 0);
}

int arg_after(int s, int skip) {
  int i;
  i = 0;
  while (i < skip) {
    if (loadb(s + i) == 0) {
      return s + i;
    }
    i = i + 1;
  }
  while (loadb(s + i) == 32) {
    i = i + 1;
  }
  return s + i;
}

void join_cwd(int dst, int name) {
  int i;
  int j;
  i = 0;
  if (loadb(name) == 47) {
    while (loadb(name + i) != 0) {
      storeb(dst + i, loadb(name + i));
      i = i + 1;
    }
    storeb(dst + i, 0);
    return;
  }
  while (loadb(g_cwd + i) != 0) {
    storeb(dst + i, loadb(g_cwd + i));
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

void cmd_ls() {
  int i;
  int got;
  char ent[64];
  int n;
  int p;
  int ok;
  int c;
  i = 0;
  n = 0;
  while (i < 48) {
    got = readdir(g_cwd, i, ent);
    if (got <= 0) {
      i = 48;
    } else {
      ok = 1;
      p = 0;
      while (loadb(ent + p) != 0) {
        c = loadb(ent + p);
        if (c < 32) {
          ok = 0;
        }
        if (c > 126) {
          ok = 0;
        }
        p = p + 1;
      }
      if (ok) {
        if (p > 0) {
          log_line(ent);
          n = n + 1;
        }
      }
      i = i + 1;
    }
  }
  if (n == 0) {
    log_line("(empty)");
  }
}

void run_line() {
  int arg;
  int st;
  int i;
  if (g_n < 1) {
    return;
  }
  log_line(g_line);
  if (starts_word(g_line, "help")) {
    log_line("cd pwd ls run cc make doom reload");
    return;
  }
  if (starts_word(g_line, "pwd")) {
    if (loadb(g_cwd) == 0) {
      log_line("/");
    } else {
      log_line(g_cwd);
    }
    return;
  }
  if (starts_word(g_line, "cd")) {
    arg = arg_after(g_line, 2);
    if (loadb(arg) == 0) {
      set_cwd("");
      log_line("ok /");
      return;
    }
    if (starts_word(arg, "..")) {
      set_cwd("");
      log_line("ok /");
      return;
    }
    join_cwd(g_tmp, arg);
    if (isdir(g_tmp)) {
      set_cwd(g_tmp);
      log_line(g_tmp);
    } else {
      set_cwd(g_tmp);
      log_line(g_tmp);
    }
    return;
  }
  if (starts_word(g_line, "ls")) {
    cmd_ls();
    return;
  }
  if (starts_word(g_line, "clear")) {
    log_clear();
    log_line("ChrisOS shell - help");
    return;
  }
  if (starts_word(g_line, "doom")) {
    st = app_spawn("GAMES/DOOM/ENGINE.CLV");
    if (st == 0) {
      log_line("run doom fail");
    } else {
      log_line("doom ok");
    }
    return;
  }
  if (starts_word(g_line, "reload")) {
    arg = arg_after(g_line, 6);
    if (loadb(arg) == 0) {
      arg = "LIB/WIN.CLS";
    }
    st = lib_reload(arg);
    if (st < 0) {
      log_line("reload fail");
    } else {
      log_line("reload ok");
    }
    return;
  }
  if (starts_word(g_line, "run")) {
    arg = arg_after(g_line, 3);
    join_cwd(g_tmp, arg);
    st = sys_run(g_tmp);
    if (st == 0) {
      sys_err(g_err);
      if (loadb(g_err) != 0) {
        log_line(g_err);
      }
      log_line("run fail");
    } else {
      log_line("run ok");
    }
    return;
  }
  if (starts_word(g_line, "cc")) {
    arg = arg_after(g_line, 2);
    join_cwd(g_tmp, arg);
    st = sys_cc(g_tmp);
    if (st == 0) {
      sys_err(g_err);
      if (loadb(g_err) != 0) {
        log_line(g_err);
      }
      log_line("cc fail");
    } else {
      log_line("cc ok");
    }
    return;
  }
  if (starts_word(g_line, "make")) {
    arg = arg_after(g_line, 4);
    if (loadb(arg) == 0) {
      st = sys_make("Makefile", "all");
    } else {
      i = 0;
      while (loadb(arg + i) != 0) {
        if (loadb(arg + i) == 32) {
          storeb(arg + i, 0);
          st = sys_make(arg, arg + i + 1);
          i = 999;
        } else {
          i = i + 1;
        }
      }
      if (i != 999) {
        st = sys_make(arg, "all");
      }
    }
    if (st == 0) {
      sys_err(g_err);
      if (loadb(g_err) != 0) {
        log_line(g_err);
      }
      log_line("make fail");
    } else {
      log_line("make ok");
    }
    return;
  }
  log_line("?");
}

void main() {
  int k;
  int ch;
  int live;
  int py;
  win_origin(160, 72);
  cwd_init();
  g_n = 0;
  log_clear();
  storeb(g_line, 0);
  log_line("ChrisOS shell - help");
  log_line("type ls   then Enter");
  while (1) {
    live = win_begin(560, 340, "Shell");
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
      if (ch >= 32) {
        if (ch <= 126) {
          if (g_n < 158) {
            storeb(g_line + g_n, ch);
            g_n = g_n + 1;
            storeb(g_line + g_n, 0);
          }
        }
      }
      ch = ev_text();
    }
    py = 316;
    text(8, 28, g_log, 0x00C0D8F0);
    text(8, py, ">", 0x00B0FFB0);
    text(20, py, g_line, 0x00FFFFFF);
    win_end();
    wait(1);
  }
}
