char g_line[160];
int g_n;
int g_log;
int g_log_cap;
int g_log_len;
int g_log_scroll;
char g_cwd[96];
char g_err[160];
char g_tmp[160];
char g_num[16];
char g_hist[1280];
int g_hist_n;
int g_hist_cur;

void put_num(int value, int dst) {
  int i;
  int d;
  int n;
  char tmp[16];
  if (value < 0) {
    value = 0;
  }
  if (value == 0) {
    storeb(dst, 48);
    storeb(dst + 1, 0);
    return;
  }
  i = 0;
  while (value > 0) {
    d = value - (value / 10) * 10;
    storeb(tmp + i, 48 + d);
    value = value / 10;
    i = i + 1;
  }
  n = 0;
  while (i > 0) {
    i = i - 1;
    storeb(dst + n, loadb(tmp + i));
    n = n + 1;
  }
  storeb(dst + n, 0);
}

void log_init() {
  if (g_log == 0) {
    g_log_cap = 49152;
    g_log = malloc(g_log_cap);
    if (g_log == 0) {
      g_log_cap = 8192;
      g_log = malloc(g_log_cap);
    }
  }
  g_log_len = 0;
  if (g_log != 0) {
    storeb(g_log, 0);
  }
  g_log_scroll = 0;
}

void log_clear() {
  g_log_len = 0;
  if (g_log != 0) {
    storeb(g_log, 0);
  }
  g_log_scroll = 0;
}

void log_trim() {
  int i;
  int drop;
  if (g_log == 0) {
    return;
  }
  if (g_log_len < g_log_cap - 256) {
    return;
  }
  drop = 4096;
  if (drop > g_log_len) {
    drop = g_log_len / 2;
  }
  i = 0;
  while (i + drop < g_log_len) {
    storeb(g_log + i, loadb(g_log + i + drop));
    i = i + 1;
  }
  g_log_len = g_log_len - drop;
  storeb(g_log + g_log_len, 0);
}

void log_line(int msg) {
  int i;
  int c;
  if (g_log == 0) {
    return;
  }
  log_trim();
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
    if (i > 400) {
      storeb(g_log + g_log_len, 10);
      g_log_len = g_log_len + 1;
      storeb(g_log + g_log_len, 0);
      return;
    }
  }
}

int log_lines() {
  int i;
  int n;
  n = 0;
  i = 0;
  while (i < g_log_len) {
    if (loadb(g_log + i) == 10) {
      n = n + 1;
    }
    i = i + 1;
  }
  return n;
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

void hist_save() {
  int slot;
  int i;
  if (g_n < 1) {
    return;
  }
  slot = (g_hist_n - (g_hist_n / 8) * 8) * 160;
  i = 0;
  while (i < 159) {
    storeb(g_hist + slot + i, loadb(g_line + i));
    if (loadb(g_line + i) == 0) {
      i = 159;
    } else {
      i = i + 1;
    }
  }
  storeb(g_hist + slot + 159, 0);
  g_hist_n = g_hist_n + 1;
  g_hist_cur = g_hist_n;
}

void hist_load(int idx) {
  int slot;
  int i;
  int maxn;
  maxn = g_hist_n;
  if (maxn > 8) {
    maxn = 8;
  }
  if (idx < g_hist_n - maxn) {
    idx = g_hist_n - maxn;
  }
  if (idx < 0) {
    idx = 0;
  }
  if (idx >= g_hist_n) {
    g_n = 0;
    storeb(g_line, 0);
    g_hist_cur = g_hist_n;
    return;
  }
  slot = (idx - (idx / 8) * 8) * 160;
  i = 0;
  while (i < 158) {
    storeb(g_line + i, loadb(g_hist + slot + i));
    if (loadb(g_hist + slot + i) == 0) {
      i = 158;
    } else {
      i = i + 1;
    }
  }
  storeb(g_line + 158, 0);
  g_n = 0;
  while (loadb(g_line + g_n) != 0) {
    g_n = g_n + 1;
  }
  g_hist_cur = idx;
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
  while (i < 512) {
    got = readdir(g_cwd, i, ent);
    if (got <= 0) {
      i = 512;
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

void cmd_cat(int name) {
  int fd;
  int n;
  char chunk[200];
  int loops;
  join_cwd(g_tmp, name);
  fd = fopen(g_tmp);
  if (fd < 0) {
    log_line("cat fail");
    return;
  }
  loops = 0;
  while (loops < 64) {
    n = fread(fd, chunk, 180);
    if (n <= 0) {
      loops = 64;
    } else {
      storeb(chunk + n, 0);
      log_line(chunk);
      loops = loops + 1;
    }
  }
  fclose(fd);
}

void run_line() {
  int arg;
  int st;
  int i;
  if (g_n < 1) {
    return;
  }
  hist_save();
  log_line(g_line);
  if (starts_word(g_line, "help")) {
    log_line("help pwd cd ls cat mkdir rm clear");
    log_line("cc  cc -c  make  run  doom  world");
    log_line("heap fps reload");
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
      log_line("cd fail");
    }
    return;
  }
  if (starts_word(g_line, "ls")) {
    cmd_ls();
    return;
  }
  if (starts_word(g_line, "cat")) {
    arg = arg_after(g_line, 3);
    if (loadb(arg) == 0) {
      log_line("cat <file>");
    } else {
      cmd_cat(arg);
    }
    return;
  }
  if (starts_word(g_line, "mkdir")) {
    arg = arg_after(g_line, 5);
    join_cwd(g_tmp, arg);
    st = mkdir(g_tmp);
    if (st == 0) {
      log_line("mkdir ok");
    } else {
      log_line("mkdir fail");
    }
    return;
  }
  if (starts_word(g_line, "rm")) {
    arg = arg_after(g_line, 2);
    join_cwd(g_tmp, arg);
    st = unlink(g_tmp);
    if (st == 0) {
      log_line("rm ok");
    } else {
      log_line("rm fail");
    }
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
      sys_err(g_err);
      if (loadb(g_err) != 0) {
        log_line(g_err);
      }
      log_line("run doom fail");
    } else {
      log_line("doom ok");
    }
    return;
  }
  if (starts_word(g_line, "world")) {
    st = app_spawn("GAMES/WORLD.CLV");
    if (st == 0) {
      sys_err(g_err);
      if (loadb(g_err) != 0) {
        log_line(g_err);
      }
      log_line("world fail");
    } else {
      log_line("world ok");
    }
    return;
  }
  if (starts_word(g_line, "heap")) {
    put_num(sys_heap_used_kb(), g_num);
    log_line(g_num);
    put_num(sys_heap_free_kb(), g_num);
    log_line(g_num);
    return;
  }
  if (starts_word(g_line, "fps")) {
    put_num(fps(), g_num);
    log_line(g_num);
    put_num(sys_frame_p50(), g_num);
    log_line(g_num);
    put_num(sys_frame_p95(), g_num);
    log_line(g_num);
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
    if (starts_word(arg, "-c")) {
      arg = arg_after(arg, 2);
    }
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
      st = 0;
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

void draw_log() {
  int view;
  int total;
  int max_off;
  int start;
  int i;
  int n;
  int y;
  int c;
  char row[96];
  int rn;
  int list_h;
  list_h = win_h() - 48;
  view = list_h / 16;
  if (view < 1) {
    view = 1;
  }
  total = log_lines();
  max_off = total - view;
  if (max_off < 0) {
    max_off = 0;
  }
  if (g_log_scroll > max_off) {
    g_log_scroll = max_off;
  }
  g_log_scroll = win_vscroll(win_w() - 18, 24, list_h, max_off, view, g_log_scroll);
  start = g_log_scroll;
  i = 0;
  n = 0;
  y = 24;
  rn = 0;
  storeb(row, 0);
  while (i <= g_log_len) {
    c = loadb(g_log + i);
    if (c == 10) {
      if (n >= start) {
        if (n < start + view) {
          storeb(row + rn, 0);
          text(8, y, row, 0x00C0D8F0);
          y = y + 16;
        }
      }
      n = n + 1;
      rn = 0;
      storeb(row, 0);
    } else {
      if (c != 0) {
        if (rn < 90) {
          storeb(row + rn, c);
          rn = rn + 1;
        }
      }
    }
    i = i + 1;
  }
}

void main() {
  int k;
  int ch;
  int live;
  int py;
  win_origin(160, 72);
  cwd_init();
  g_n = 0;
  g_hist_n = 0;
  g_hist_cur = 0;
  log_init();
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
        g_log_scroll = 99999;
      }
      if (k == 7) {
        if (g_hist_n > 0) {
          hist_load(g_hist_cur - 1);
        }
      }
      if (k == 8) {
        hist_load(g_hist_cur + 1);
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
    draw_log();
    py = win_h() - 22;
    fillrgb(6, py, win_w() - 28, 16, 0x00202838);
    text(8, py, ">", 0x00B0FFB0);
    text(20, py, g_line, 0x00FFFFFF);
    win_end();
    wait(1);
  }
}
