/* ChrisOS Editor ??? mini-VI
 * Modes: Normal / Insert / Command (: and /)
 * Esc=Normal  i/a/o=Insert  :w :q :wq :w path  /pat n N
 * hjkl arrows  0 $ gg G  x dd  Open SaveAs Compile Go
 */
int g_buf;
int g_cap;
int g_len;
int g_cur;
char g_path[96];
char g_dir[96];
char g_err[160];
char g_ent[64];
char g_show[80];
char g_cmd[80];
int g_cmd_n;
int g_cmd_kind;
char g_find[64];
char g_status[80];
char g_lnum[8];
char g_panel[512];
char g_run[96];
int g_err_line;
int g_mode;
int g_pending;
int g_dirty;
int g_msg;
int g_browse;
int g_saveas;
int g_nshow;
int g_ntotal;
int g_prev;
int g_scroll;
int g_hscroll;
int g_tscroll;
int g_thscroll;
int g_want_quit;
int g_line_starts[2048];
int g_line_states[2048];
int g_line_count;
int g_line_index_dirty;

void ensure_line_index();
void buf_init() {
  if (g_buf == 0) {
    g_cap = 262144;
    g_buf = malloc(g_cap);
    if (g_buf == 0) {
      g_cap = 65536;
      g_buf = malloc(g_cap);
    }
    if (g_buf == 0) {
      g_cap = 8192;
      g_buf = malloc(g_cap);
    }
  }
  if (g_buf != 0) {
    storeb(g_buf, 0);
  }
  g_len = 0;
  storeb(g_panel, 0);
  g_err_line = 0;
}

void str_set(int dst, int src) {
  int i;
  i = 0;
  while (loadb(src + i) != 0) {
    storeb(dst + i, loadb(src + i));
    i = i + 1;
  }
  storeb(dst + i, 0);
}

void str_clear(int s) {
  storeb(s, 0);
}

int name_len(int s) {
  int n;
  n = 0;
  while (loadb(s + n) != 0) {
    n = n + 1;
  }
  return n;
}

void status_set(int s) {
  str_set(g_status, s);
}

void path_set(int p) {
  int i;
  i = 0;
  while (loadb(p + i) != 0) {
    if (i < 95) {
      storeb(g_path + i, loadb(p + i));
    }
    i = i + 1;
  }
  storeb(g_path + i, 0);
}

void dir_set(int p) {
  int i;
  i = 0;
  while (loadb(p + i) != 0) {
    if (i < 95) {
      storeb(g_dir + i, loadb(p + i));
    }
    i = i + 1;
  }
  storeb(g_dir + i, 0);
  g_scroll = 0;
  g_hscroll = 0;
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
    if (j < 60) {
      storeb(g_show + j, loadb(src + i));
      j = j + 1;
    }
    i = i + 1;
  }
  storeb(g_show + j, 0);
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
      if (loadb(s + n - 3) == 76) {
        if (loadb(s + n - 2) == 83) {
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

void parent_dir() {
  int n;
  int i;
  int slash;
  n = name_len(g_dir);
  if (n <= 0) {
    return;
  }
  slash = -1;
  i = 0;
  while (i < n) {
    if (loadb(g_dir + i) == 47) {
      slash = i;
    }
    i = i + 1;
  }
  if (slash < 0) {
    storeb(g_dir, 0);
  } else {
    storeb(g_dir + slash, 0);
  }
  g_scroll = 0;
}

int is_source() {
  int n;
  n = name_len(g_path);
  if (n < 3) {
    return 0;
  }
  if (loadb(g_path + n - 3) == 46) {
    if (loadb(g_path + n - 2) == 67) {
      if (loadb(g_path + n - 1) == 67) {
        return 1;
      }
    }
  }
  if (n >= 4) {
    if (loadb(g_path + n - 4) == 46) {
      if (loadb(g_path + n - 3) == 76) {
        return 1;
      }
    }
  }
  return 0;
}

int word_eq(int at, int w) {
  int i;
  int c;
  int b;
  i = 0;
  while (loadb(w + i) != 0) {
    if (at + i >= g_len) {
      return 0;
    }
    if (loadb(g_buf + at + i) != loadb(w + i)) {
      return 0;
    }
    i = i + 1;
  }
  if (at > 0) {
    b = loadb(g_buf + at - 1);
    if (b >= 97) {
      if (b <= 122) {
        return 0;
      }
    }
    if (b >= 65) {
      if (b <= 90) {
        return 0;
      }
    }
    if (b >= 48) {
      if (b <= 57) {
        return 0;
      }
    }
    if (b == 95) {
      return 0;
    }
  }
  c = loadb(g_buf + at + i);
  if (c >= 97) {
    if (c <= 122) {
      return 0;
    }
  }
  if (c >= 65) {
    if (c <= 90) {
      return 0;
    }
  }
  if (c >= 48) {
    if (c <= 57) {
      return 0;
    }
  }
  if (c == 95) {
    return 0;
  }
  return 1;
}

int is_kw_at(int at) {
  if (word_eq(at, "int")) {
    return 1;
  }
  if (word_eq(at, "void")) {
    return 1;
  }
  if (word_eq(at, "char")) {
    return 1;
  }
  if (word_eq(at, "if")) {
    return 1;
  }
  if (word_eq(at, "else")) {
    return 1;
  }
  if (word_eq(at, "while")) {
    return 1;
  }
  if (word_eq(at, "for")) {
    return 1;
  }
  if (word_eq(at, "return")) {
    return 1;
  }
  if (word_eq(at, "break")) {
    return 1;
  }
  if (word_eq(at, "continue")) {
    return 1;
  }
  return 0;
}

int kw_len_at(int at) {
  if (word_eq(at, "continue")) {
    return 8;
  }
  if (word_eq(at, "return")) {
    return 6;
  }
  if (word_eq(at, "while")) {
    return 5;
  }
  if (word_eq(at, "break")) {
    return 5;
  }
  if (word_eq(at, "else")) {
    return 4;
  }
  if (word_eq(at, "void")) {
    return 4;
  }
  if (word_eq(at, "char")) {
    return 4;
  }
  if (word_eq(at, "int")) {
    return 3;
  }
  if (word_eq(at, "for")) {
    return 3;
  }
  if (word_eq(at, "if")) {
    return 2;
  }
  return 0;
}

void load_file() {
  int fd;
  int n;
  if (g_buf == 0) {
    g_msg = 8;
    status_set("no buffer");
    return;
  }
  fd = fopen(g_path);
  if (fd < 0) {
    g_msg = 8;
    status_set("open fail");
    return;
  }
  n = fread(fd, g_buf, g_cap - 1);
  fclose(fd);
  if (n < 0) {
    n = 0;
  }
  g_len = n;
  storeb(g_buf + g_len, 0);
  g_cur = 0;
  g_line_index_dirty = 1;
  g_msg = 0;
  g_browse = 0;
  g_saveas = 0;
  g_tscroll = 0;
  g_thscroll = 0;
  g_dirty = 0;
  g_mode = 0;
  status_set("");
}

void save_file() {
  int fd;
  fd = fopen(g_path);
  if (fd < 0) {
    g_msg = 2;
    status_set("save fail");
    return;
  }
  fwrite(fd, g_buf, g_len);
  fclose(fd);
  g_msg = 1;
  g_dirty = 0;
  status_set("written");
}

void capture_err() {
  int i;
  int n;
  int c;
  sys_err(g_err);
  i = 0;
  while (loadb(g_err + i) != 0) {
    if (i < 500) {
      storeb(g_panel + i, loadb(g_err + i));
    }
    i = i + 1;
  }
  if (i > 500) {
    i = 500;
  }
  storeb(g_panel + i, 0);
  g_err_line = 0;
  i = 0;
  while (loadb(g_err + i) != 0) {
    if (loadb(g_err + i) == 58) {
      n = 0;
      i = i + 1;
      while (1) {
        c = loadb(g_err + i);
        if (c >= 48) {
          if (c <= 57) {
            n = n * 10 + (c - 48);
            i = i + 1;
          } else {
            break;
          }
        } else {
          break;
        }
      }
      if (n > 0) {
        g_err_line = n;
      }
    } else {
      i = i + 1;
    }
  }
}

void jump_line(int ln) {
  ensure_line_index();
  if (ln < 1) {
    return;
  }
  if (ln > g_line_count) {
    ln = g_line_count;
  }
  g_cur = g_line_starts[ln - 1];
  ensure_view();
}

void do_compile() {
  int st;
  if (is_source() == 0) {
    g_msg = 3;
    status_set("not source");
    return;
  }
  st = sys_cc(g_path);
  if (st == 0) {
    capture_err();
    g_msg = 4;
    status_set(g_err);
  } else {
    storeb(g_panel, 0);
    g_err_line = 0;
    g_msg = 5;
    status_set("compile ok");
  }
}

void do_make() {
  int st;
  st = sys_make("Makefile", "all");
  if (st == 0) {
    capture_err();
    g_msg = 4;
    status_set(g_err);
  } else {
    storeb(g_panel, 0);
    g_err_line = 0;
    g_msg = 5;
    status_set("make ok");
  }
}

void do_go() {
  int st;
  char out[96];
  int i;
  int dot;
  if (g_dirty) {
    save_file();
    if (g_msg == 2) {
      return;
    }
  }
  do_compile();
  if (g_msg == 4) {
    return;
  }
  if (g_msg == 3) {
    return;
  }
  i = 0;
  dot = -1;
  while (loadb(g_path + i) != 0) {
    storeb(out + i, loadb(g_path + i));
    if (loadb(g_path + i) == 46) {
      dot = i;
    }
    i = i + 1;
  }
  if (dot < 0) {
    status_set("no ext");
    return;
  }
  storeb(out + dot + 1, 67);
  storeb(out + dot + 2, 76);
  storeb(out + dot + 3, 86);
  storeb(out + dot + 4, 0);
  st = sys_run(out);
  if (st == 0) {
    capture_err();
    g_msg = 6;
    status_set(g_err);
  } else {
    storeb(g_panel, 0);
    g_msg = 7;
    status_set("running");
  }
}

void browse_open() {
  int i;
  int slash;
  g_saveas = 0;
  i = 0;
  slash = -1;
  while (loadb(g_path + i) != 0) {
    if (loadb(g_path + i) == 47) {
      slash = i;
    }
    i = i + 1;
  }
  if (slash < 0) {
    dir_set("");
  } else {
    i = 0;
    while (i < slash) {
      storeb(g_dir + i, loadb(g_path + i));
      i = i + 1;
    }
    storeb(g_dir + slash, 0);
  }
  g_browse = 1;
  g_msg = 0;
  g_scroll = 0;
  status_set("open");
}

void browse_saveas() {
  int i;
  int slash;
  g_saveas = 1;
  dir_set("");
  if (name_len(g_path) > 0) {
    i = 0;
    slash = -1;
    while (loadb(g_path + i) != 0) {
      if (loadb(g_path + i) == 47) {
        slash = i;
      }
      i = i + 1;
    }
    if (slash >= 0) {
      i = 0;
      while (i < slash) {
        storeb(g_dir + i, loadb(g_path + i));
        i = i + 1;
      }
      storeb(g_dir + slash, 0);
    }
  }
  g_browse = 1;
  g_scroll = 0;
  status_set("save as: pick file or :w name");
}

void browse_pick(int name) {
  char full[160];
  join_path(full, g_dir, name);
  if (g_saveas == 0) {
    if (is_textish(name)) {
      path_set(full);
      load_file();
      return;
    }
  }
  if (isdir(full)) {
    dir_set(full);
    return;
  }
  if (g_saveas) {
    if (name_len(name) > 0) {
      if (loadb(name) != 46) {
        if (is_textish(name)) {
          path_set(full);
          save_file();
          g_browse = 0;
          g_saveas = 0;
          return;
        }
      }
    }
  }
  if (name_len(name) > 0) {
    if (loadb(name) != 46) {
      dir_set(full);
    }
  }
}

void rebuild_index() {
  int i;
  int got;
  g_ntotal = 0;
  i = 0;
  while (i < 512) {
    got = readdir(g_dir, i, g_ent);
    if (got <= 0) {
      return;
    }
    if (name_ok(g_ent)) {
      g_ntotal = g_ntotal + 1;
    }
    i = i + 1;
  }
}

int entry_at(int want) {
  int i;
  int got;
  int n;
  n = 0;
  i = 0;
  while (i < 512) {
    got = readdir(g_dir, i, g_ent);
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

void rebuild_line_index() {
  int i;
  int c;
  int in_str;
  int in_lc;
  int in_blk;
  g_line_count = 1;
  g_line_starts[0] = 0;
  g_line_states[0] = 0;
  in_str = 0;
  in_lc = 0;
  in_blk = 0;
  i = 0;
  while (i < g_len) {
    c = loadb(g_buf + i);
    if (c == 10) {
      in_str = 0;
      in_lc = 0;
      if (g_line_count < 2048) {
        g_line_starts[g_line_count] = i + 1;
        g_line_states[g_line_count] = in_blk * 2;
        g_line_count = g_line_count + 1;
      }
    } else {
      if (in_lc == 0) {
        if (in_blk) {
          if (c == 42) {
            if (i + 1 < g_len) {
              if (loadb(g_buf + i + 1) == 47) {
                in_blk = 0;
                i = i + 1;
              }
            }
          }
        } else {
          if (in_str) {
            if (c == 92) {
              if (i + 1 < g_len) {
                i = i + 1;
              }
            } else {
              if (c == 34) {
                in_str = 0;
              }
            }
          } else {
            if (c == 34) {
              in_str = 1;
            } else {
              if (c == 47) {
                if (i + 1 < g_len) {
                  if (loadb(g_buf + i + 1) == 47) {
                    in_lc = 1;
                  }
                  if (loadb(g_buf + i + 1) == 42) {
                    in_blk = 1;
                  }
                }
              }
            }
          }
        }
      }
    }
    i = i + 1;
  }
  g_line_index_dirty = 0;
}

void ensure_line_index() {
  if (g_line_index_dirty) {
    rebuild_line_index();
  }
}

int line_for_pos(int p) {
  int lo;
  int hi;
  int mid;
  ensure_line_index();
  if (p < 0) {
    p = 0;
  }
  if (p > g_len) {
    p = g_len;
  }
  lo = 0;
  hi = g_line_count - 1;
  while (lo < hi) {
    mid = (lo + hi + 1) / 2;
    if (g_line_starts[mid] <= p) {
      lo = mid;
    } else {
      hi = mid - 1;
    }
  }
  return lo;
}

int count_lines() {
  ensure_line_index();
  return g_line_count;
}

int line_start(int p) {
  return g_line_starts[line_for_pos(p)];
}

int line_end(int p) {
  int line;
  line = line_for_pos(p);
  if (line + 1 < g_line_count) {
    return g_line_starts[line + 1] - 1;
  }
  return g_len;
}

int cur_line_num() {
  return line_for_pos(g_cur) + 1;
}

int cur_col_num() {
  return g_cur - line_start(g_cur);
}

void clamp_cur() {
  if (g_cur < 0) {
    g_cur = 0;
  }
  if (g_cur > g_len) {
    g_cur = g_len;
  }
}

void ensure_view() {
  int ln;
  int view;
  view = (win_h() - 68 - 90) / 16;
  if (view < 1) {
    view = 1;
  }
  ln = cur_line_num() - 1;
  if (ln < g_tscroll) {
    g_tscroll = ln;
  }
  if (ln >= g_tscroll + view) {
    g_tscroll = ln - view + 1;
  }
  if (g_tscroll < 0) {
    g_tscroll = 0;
  }
}

void move_left() {
  if (g_cur > 0) {
    g_cur = g_cur - 1;
  }
  ensure_view();
}

void move_right() {
  if (g_cur < g_len) {
    g_cur = g_cur + 1;
  }
  ensure_view();
}

void move_up() {
  int col;
  int p;
  int s;
  col = cur_col_num();
  s = line_start(g_cur);
  if (s == 0) {
    return;
  }
  p = line_start(s - 1);
  g_cur = p;
  while (col > 0) {
    if (g_cur >= s - 1) {
      col = 0;
    } else {
      if (loadb(g_buf + g_cur) == 10) {
        col = 0;
      } else {
        g_cur = g_cur + 1;
        col = col - 1;
      }
    }
  }
  ensure_view();
}

void move_down() {
  int col;
  int e;
  int p;
  col = cur_col_num();
  e = line_end(g_cur);
  if (e >= g_len) {
    return;
  }
  p = e + 1;
  g_cur = p;
  while (col > 0) {
    if (g_cur >= g_len) {
      col = 0;
    } else {
      if (loadb(g_buf + g_cur) == 10) {
        col = 0;
      } else {
        g_cur = g_cur + 1;
        col = col - 1;
      }
    }
  }
  ensure_view();
}

void insert_at(int ch) {
  int i;
  if (g_len >= g_cap - 2) {
    return;
  }
  i = g_len;
  while (i > g_cur) {
    storeb(g_buf + i, loadb(g_buf + i - 1));
    i = i - 1;
  }
  storeb(g_buf + g_cur, ch);
  g_len = g_len + 1;
  storeb(g_buf + g_len, 0);
  g_cur = g_cur + 1;
  g_dirty = 1;
  g_line_index_dirty = 1;
  ensure_view();
}

void delete_at() {
  int i;
  if (g_cur >= g_len) {
    return;
  }
  i = g_cur;
  while (i < g_len) {
    storeb(g_buf + i, loadb(g_buf + i + 1));
    i = i + 1;
  }
  g_len = g_len - 1;
  storeb(g_buf + g_len, 0);
  g_dirty = 1;
  g_line_index_dirty = 1;
}

void delete_line() {
  int s;
  int e;
  int i;
  int n;
  s = line_start(g_cur);
  e = line_end(g_cur);
  if (e < g_len) {
    e = e + 1;
  }
  n = e - s;
  i = s;
  while (i + n <= g_len) {
    storeb(g_buf + i, loadb(g_buf + i + n));
    i = i + 1;
  }
  g_len = g_len - n;
  if (g_len < 0) {
    g_len = 0;
  }
  storeb(g_buf + g_len, 0);
  g_cur = s;
  if (g_cur > g_len) {
    g_cur = g_len;
  }
  g_dirty = 1;
  g_line_index_dirty = 1;
  ensure_view();
}

void open_line_below() {
  int e;
  e = line_end(g_cur);
  g_cur = e;
  insert_at(10);
  g_mode = 1;
}

void open_line_above() {
  int s;
  s = line_start(g_cur);
  g_cur = s;
  insert_at(10);
  g_cur = s;
  g_mode = 1;
}

int find_from(int start, int dir) {
  int fl;
  int i;
  int j;
  int ok;
  fl = name_len(g_find);
  if (fl < 1) {
    return -1;
  }
  if (dir > 0) {
    i = start;
    while (i + fl <= g_len) {
      ok = 1;
      j = 0;
      while (j < fl) {
        if (loadb(g_buf + i + j) != loadb(g_find + j)) {
          ok = 0;
          j = fl;
        } else {
          j = j + 1;
        }
      }
      if (ok) {
        return i;
      }
      i = i + 1;
    }
  } else {
    i = start;
    while (i >= 0) {
      if (i + fl <= g_len) {
        ok = 1;
        j = 0;
        while (j < fl) {
          if (loadb(g_buf + i + j) != loadb(g_find + j)) {
            ok = 0;
            j = fl;
          } else {
            j = j + 1;
          }
        }
        if (ok) {
          return i;
        }
      }
      i = i - 1;
    }
  }
  return -1;
}

void find_next() {
  int p;
  p = find_from(g_cur + 1, 1);
  if (p < 0) {
    p = find_from(0, 1);
  }
  if (p < 0) {
    status_set("not found");
  } else {
    g_cur = p;
    ensure_view();
    status_set("match");
  }
}

void find_prev() {
  int p;
  p = find_from(g_cur - 1, -1);
  if (p < 0) {
    p = find_from(g_len, -1);
  }
  if (p < 0) {
    status_set("not found");
  } else {
    g_cur = p;
    ensure_view();
    status_set("match");
  }
}

void put_num(int n, int dst) {
  int i;
  int v;
  int d;
  char tmp[8];
  if (n < 0) {
    n = 0;
  }
  if (n == 0) {
    storeb(dst, 48);
    storeb(dst + 1, 0);
    return;
  }
  v = n;
  i = 0;
  while (v > 0) {
    d = v - (v / 10) * 10;
    storeb(tmp + i, 48 + d);
    i = i + 1;
    v = v / 10;
  }
  while (i > 0) {
    i = i - 1;
    storeb(dst, loadb(tmp + i));
    dst = dst + 1;
  }
  storeb(dst, 0);
}

void run_colon() {
  int i;
  int c0;
  int c1;
  if (g_cmd_n < 1) {
    g_mode = 0;
    return;
  }
  c0 = loadb(g_cmd);
  c1 = loadb(g_cmd + 1);
  if (c0 == 113) {
    if (c1 == 0) {
      if (g_dirty) {
        status_set("unsaved ??? :q! to quit");
        g_mode = 0;
        return;
      }
      g_want_quit = 1;
      return;
    }
    if (c1 == 33) {
      g_want_quit = 1;
      return;
    }
  }
  if (c0 == 119) {
    if (c1 == 0) {
      save_file();
      g_mode = 0;
      return;
    }
    if (c1 == 113) {
      save_file();
      g_want_quit = 1;
      return;
    }
    if (c1 == 32) {
      i = 2;
      while (loadb(g_cmd + i) == 32) {
        i = i + 1;
      }
      if (loadb(g_cmd + i) != 0) {
        path_set(g_cmd + i);
        save_file();
        g_mode = 0;
        return;
      }
    }
  }
  if (c0 == 101) {
    if (c1 == 32) {
      i = 2;
      while (loadb(g_cmd + i) == 32) {
        i = i + 1;
      }
      if (loadb(g_cmd + i) != 0) {
        path_set(g_cmd + i);
        load_file();
        g_mode = 0;
        return;
      }
    }
  }
  status_set("bad cmd");
  g_mode = 0;
}

void run_search_cmd() {
  if (g_cmd_n < 1) {
    g_mode = 0;
    return;
  }
  str_set(g_find, g_cmd);
  g_mode = 0;
  find_next();
}

void start_cmdline(int kind) {
  g_mode = 2;
  g_cmd_kind = kind;
  g_cmd_n = 0;
  storeb(g_cmd, 0);
  g_pending = 0;
}

void handle_normal_char(int ch) {
  if (g_pending == 100) {
    g_pending = 0;
    if (ch == 100) {
      delete_line();
      status_set("deleted line");
    }
    return;
  }
  if (g_pending == 103) {
    g_pending = 0;
    if (ch == 103) {
      g_cur = 0;
      ensure_view();
    }
    return;
  }
  if (ch == 105) {
    g_mode = 1;
    status_set("-- INSERT --");
    return;
  }
  if (ch == 97) {
    if (g_cur < g_len) {
      if (loadb(g_buf + g_cur) != 10) {
        g_cur = g_cur + 1;
      }
    }
    g_mode = 1;
    status_set("-- INSERT --");
    return;
  }
  if (ch == 111) {
    open_line_below();
    status_set("-- INSERT --");
    return;
  }
  if (ch == 79) {
    open_line_above();
    status_set("-- INSERT --");
    return;
  }
  if (ch == 104) {
    move_left();
    return;
  }
  if (ch == 108) {
    move_right();
    return;
  }
  if (ch == 107) {
    move_up();
    return;
  }
  if (ch == 106) {
    move_down();
    return;
  }
  if (ch == 48) {
    g_cur = line_start(g_cur);
    ensure_view();
    return;
  }
  if (ch == 36) {
    g_cur = line_end(g_cur);
    ensure_view();
    return;
  }
  if (ch == 120) {
    delete_at();
    return;
  }
  if (ch == 100) {
    g_pending = 100;
    return;
  }
  if (ch == 103) {
    g_pending = 103;
    return;
  }
  if (ch == 71) {
    g_cur = g_len;
    ensure_view();
    return;
  }
  if (ch == 110) {
    find_next();
    return;
  }
  if (ch == 78) {
    find_prev();
    return;
  }
  if (ch == 47) {
    start_cmdline(1);
    return;
  }
  if (ch == 58) {
    start_cmdline(0);
    return;
  }
}

void handle_key(int k) {
  if (k == 4) {
    if (g_mode == 1) {
      g_mode = 0;
      status_set("");
      return;
    }
    if (g_mode == 2) {
      g_mode = 0;
      status_set("");
      return;
    }
    return;
  }
  if (g_browse) {
    return;
  }
  if (g_mode == 2) {
    if (k == 1) {
      if (g_cmd_n > 0) {
        g_cmd_n = g_cmd_n - 1;
        storeb(g_cmd + g_cmd_n, 0);
      }
      return;
    }
    if (k == 3) {
      if (g_cmd_kind == 0) {
        run_colon();
      } else {
        run_search_cmd();
      }
      return;
    }
    return;
  }
  if (k == 5) {
    move_left();
    return;
  }
  if (k == 6) {
    move_right();
    return;
  }
  if (k == 7) {
    move_up();
    return;
  }
  if (k == 8) {
    move_down();
    return;
  }
  if (k == 9) {
    g_cur = line_start(g_cur);
    ensure_view();
    return;
  }
  if (k == 10) {
    g_cur = line_end(g_cur);
    ensure_view();
    return;
  }
  if (k == 11) {
    delete_at();
    return;
  }
  if (g_mode == 1) {
    if (k == 1) {
      if (g_cur > 0) {
        g_cur = g_cur - 1;
        delete_at();
      }
      return;
    }
    if (k == 3) {
      insert_at(10);
      return;
    }
    if (k == 2) {
      insert_at(32);
      insert_at(32);
      return;
    }
    return;
  }
  if (k == 12) {
    save_file();
    return;
  }
  if (k == 13) {
    browse_open();
    return;
  }
  if (k == 14) {
    do_compile();
    return;
  }
  if (k == 15) {
    do_go();
    return;
  }
}

void handle_text(int ch) {
  if (g_browse) {
    return;
  }
  if (g_mode == 2) {
    if (ch >= 32) {
      if (ch < 127) {
        if (g_cmd_n < 78) {
          storeb(g_cmd + g_cmd_n, ch);
          g_cmd_n = g_cmd_n + 1;
          storeb(g_cmd + g_cmd_n, 0);
        }
      }
    }
    return;
  }
  if (g_mode == 1) {
    if (ch >= 32) {
      if (ch < 127) {
        insert_at(ch);
      }
    }
    return;
  }
  if (ch >= 32) {
    if (ch < 127) {
      handle_normal_char(ch);
    }
  }
}

int syn_color(int at, int in_str, int in_line_cmt, int in_blk) {
  int c;
  int kl;
  if (in_line_cmt) {
    return 0x00608070;
  }
  if (in_blk) {
    return 0x00608070;
  }
  if (in_str) {
    return 0x00E0A060;
  }
  c = loadb(g_buf + at);
  if (c == 35) {
    return 0x00C080E0;
  }
  if (c >= 48) {
    if (c <= 57) {
      return 0x00E0E060;
    }
  }
  kl = kw_len_at(at);
  if (kl > 0) {
    return 0x0060C8E0;
  }
  return 0x00E8F0FF;
}

void draw_gutter_num(int x, int y, int num) {
  put_num(num, g_lnum);
  text(x, y, g_lnum, 0x00607080);
}

void flush_run(int x, int y, int n, int color) {
  if (n < 1) {
    return;
  }
  storeb(g_run + n, 0);
  text(x, y, g_run, color);
}

void draw_panel() {
  int y;
  int h;
  h = 56;
  y = win_h() - 18 - h;
  fillrgb(6, y, win_w() - 28, h, 0x00182028);
  text(8, y + 2, "cc / make / run", 0x0080A0B0);
  if (loadb(g_panel) == 0) {
    text(8, y + 18, "(no output)", 0x00607080);
  } else {
    text(8, y + 18, g_panel, 0x00FFE080);
    if (g_err_line > 0) {
      text(8, y + 36, "click to go to error line", 0x00A0C8E0);
    }
  }
}

void draw_text_view() {
  int i;
  int line;
  int col;
  int y;
  int c;
  int view;
  int max_off;
  int list_h;
  int list_w;
  int gx;
  int px;
  int color;
  int in_str;
  int in_lc;
  int in_blk;
  int kl;
  int skip;
  int is_cur;
  int rn;
  int run_col;
  int run_x;
  int hs_y;
  hs_y = win_h() - 18 - 56 - 14;
  list_w = win_w() - 28;
  list_h = hs_y - 68;
  if (list_h < 32) {
    list_h = 32;
  }
  view = list_h / 16;
  if (view < 1) {
    view = 1;
  }
  max_off = count_lines() - view;
  if (max_off < 0) {
    max_off = 0;
  }
  if (g_tscroll > max_off) {
    g_tscroll = max_off;
  }
  g_tscroll = win_vscroll(win_w() - 18, 68, list_h, max_off, view, g_tscroll);
  g_thscroll = win_hscroll(6, hs_y, list_w, 80, 40, g_thscroll);
  fillrgb(6, 68, list_w, list_h, 0x00101828);
  gx = 8;
  ensure_line_index();
  line = g_tscroll;
  i = g_line_starts[line];
  y = 68;
  in_str = g_line_states[line] - (g_line_states[line] / 2) * 2;
  in_lc = 0;
  in_blk = g_line_states[line] / 2;
  while (y + 16 <= 68 + list_h) {
    if (line > g_tscroll + view) {
      y = win_h();
    } else {
      draw_gutter_num(gx, y, line + 1);
      px = gx + 36;
      col = 0;
      skip = 0;
      rn = 0;
      run_col = 0;
      run_x = px;
      if (i >= g_len) {
        if (g_cur == g_len) {
          if (line == cur_line_num() - 1) {
            fillrgb(px, y, 8, 14, 0x00C0D0E0);
          }
        }
        y = win_h();
      } else {
        while (i < g_len) {
          c = loadb(g_buf + i);
          if (c == 10) {
            flush_run(run_x, y, rn, run_col);
            rn = 0;
            is_cur = 0;
            if (g_cur == i) {
              is_cur = 1;
            }
            if (is_cur) {
              fillrgb(px, y, 8, 14, 0x00C0D0E0);
            }
            i = i + 1;
            in_lc = 0;
            in_str = 0;
            line = line + 1;
            break;
          }
          color = syn_color(i, in_str, in_lc, in_blk);
          kl = 0;
          if (in_str == 0) {
            if (in_lc == 0) {
              if (in_blk == 0) {
                kl = kw_len_at(i);
              }
            }
          }
          if (in_blk == 0) {
            if (in_str == 0) {
              if (c == 47) {
                if (i + 1 < g_len) {
                  if (loadb(g_buf + i + 1) == 47) {
                    in_lc = 1;
                    color = 0x00608070;
                  }
                  if (loadb(g_buf + i + 1) == 42) {
                    in_blk = 1;
                    color = 0x00608070;
                  }
                }
              }
              if (c == 34) {
                in_str = 1;
                color = 0x00E0A060;
              }
            } else {
              if (c == 34) {
                color = 0x00E0A060;
                in_str = 0;
              }
            }
          } else {
            color = 0x00608070;
            if (c == 42) {
              if (i + 1 < g_len) {
                if (loadb(g_buf + i + 1) == 47) {
                  in_blk = 0;
                }
              }
            }
          }
          if (kl > 0) {
            color = 0x0060C8E0;
            skip = kl - 1;
          }
          is_cur = 0;
          if (g_cur == i) {
            is_cur = 1;
          }
          if (col >= g_thscroll) {
            if (px + 10 < win_w() - 24) {
              if (is_cur) {
                flush_run(run_x, y, rn, run_col);
                rn = 0;
                fillrgb(px, y, 10, 14, 0x00C0D0E0);
                glyph(px, y, c, 0x00101828);
                px = px + 10;
                run_x = px;
              } else {
                if (rn == 0) {
                  run_col = color;
                  run_x = px;
                }
                if (color != run_col) {
                  flush_run(run_x, y, rn, run_col);
                  rn = 0;
                  run_col = color;
                  run_x = px;
                }
                if (rn < 90) {
                  storeb(g_run + rn, c);
                  rn = rn + 1;
                }
                px = px + 10;
              }
            }
          }
          if (skip > 0) {
            while (skip > 0) {
              i = i + 1;
              skip = skip - 1;
              c = loadb(g_buf + i);
              is_cur = 0;
              if (g_cur == i) {
                is_cur = 1;
              }
              if (col >= g_thscroll) {
                if (px + 10 < win_w() - 24) {
                  if (is_cur) {
                    flush_run(run_x, y, rn, run_col);
                    rn = 0;
                    fillrgb(px, y, 10, 14, 0x00C0D0E0);
                    glyph(px, y, c, 0x00101828);
                    px = px + 10;
                    run_x = px;
                  } else {
                    if (rn == 0) {
                      run_col = 0x0060C8E0;
                      run_x = px;
                    }
                    if (rn < 90) {
                      storeb(g_run + rn, c);
                      rn = rn + 1;
                    }
                    px = px + 10;
                  }
                }
              }
              col = col + 1;
            }
          }
          i = i + 1;
          col = col + 1;
        }
        flush_run(run_x, y, rn, run_col);
        if (g_cur == g_len) {
          if (line == cur_line_num() - 1) {
            fillrgb(px, y, 8, 14, 0x00C0D0E0);
          }
        }
        y = y + 16;
        if (i >= g_len) {
          y = win_h();
        }
      }
    }
  }
  draw_panel();
}

void draw_status() {
  int y;
  char info[48];
  int n;
  y = win_h() - 32;
  fillrgb(0, y, win_w() - 20, 16, 0x00202838);
  if (g_mode == 2) {
    if (g_cmd_kind == 0) {
      text(6, y + 1, ":", 0x00FFE080);
    } else {
      text(6, y + 1, "/", 0x00FFE080);
    }
    text(16, y + 1, g_cmd, 0x00FFFFFF);
    return;
  }
  if (g_mode == 1) {
    text(6, y + 1, "-- INSERT --", 0x0000FF80);
  } else {
    text(6, y + 1, "NORMAL", 0x00A0C8E0);
  }
  if (g_dirty) {
    text(90, y + 1, "[+]", 0x00FFE080);
  }
  put_num(cur_line_num(), info);
  n = name_len(info);
  storeb(info + n, 58);
  put_num(cur_col_num() + 1, info + n + 1);
  text(win_w() - 90, y + 1, info, 0x00A0B0C0);
  if (loadb(g_status) != 0) {
    text(130, y + 1, g_status, 0x00E0E080);
  }
}

void main() {
  int live;
  int k;
  int ch;
  int i;
  int y;
  int mb;
  int my;
  int row;
  int view;
  int max_off;
  int list_h;
  int list_w;
  char arg[96];
  win_origin(80, 40);
  buf_init();
  path_set("SRC/HELLO.TXT");
  dir_set("SRC");
  if (app_arg(arg)) {
    path_set(arg);
  }
  g_mode = 0;
  g_pending = 0;
  g_dirty = 0;
  g_want_quit = 0;
  g_cmd_n = 0;
  storeb(g_cmd, 0);
  storeb(g_find, 0);
  storeb(g_status, 0);
  load_file();
  if (g_msg == 8) {
    path_set("SRC/HELLO.TXT");
    load_file();
  }
  g_browse = 0;
  g_prev = 0;
  g_scroll = 0;
  g_hscroll = 0;
  g_tscroll = 0;
  g_thscroll = 0;
  while (1) {
    live = win_begin_keep(720, 440, "Editor");
    if (live == 0) {
      return;
    }
    if (g_want_quit) {
      surf_close();
      return;
    }
    if (win_click(8, 24, 48, 18)) {
      browse_open();
    }
    if (win_click(60, 24, 48, 18)) {
      if (g_browse == 0) {
        save_file();
      } else {
        g_browse = 0;
        g_saveas = 0;
      }
    }
    if (win_click(112, 24, 64, 18)) {
      if (g_browse == 0) {
        browse_saveas();
      }
    }
    if (win_click(180, 24, 72, 18)) {
      if (g_browse == 0) {
        do_compile();
      }
    }
    if (win_click(256, 24, 40, 18)) {
      if (g_browse == 0) {
        do_go();
      }
    }
    if (win_click(304, 24, 48, 18)) {
      if (g_browse == 0) {
        do_make();
      }
    }
    if (g_browse) {
      rebuild_index();
      list_w = win_w() - 28;
      list_h = win_h() - 88 - 18;
      view = list_h / 18;
      if (view < 1) {
        view = 1;
      }
      max_off = g_ntotal - view;
      if (max_off < 0) {
        max_off = 0;
      }
      g_scroll = win_vscroll(win_w() - 18, 88, list_h, max_off, view, g_scroll);
      g_hscroll = win_hscroll(6, win_h() - 16, list_w, 20, 20, g_hscroll);
      mb = mouse_btn();
      if (mb) {
        if (g_prev == 0) {
          if (win_mouse_in()) {
            my = mouse_y() - win_oy();
            if (my >= 88) {
              if (my < 88 + list_h) {
                row = (my - 88) / 18;
                if (row == 0) {
                  parent_dir();
                } else {
                  if (row > 0) {
                    if (row <= view) {
                      i = g_scroll + row - 1;
                      if (i >= 0) {
                        if (i < g_ntotal) {
                          if (entry_at(i)) {
                            browse_pick(g_ent);
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
    } else {
      mb = mouse_btn();
      if (mb) {
        if (g_prev == 0) {
          if (win_mouse_in()) {
            my = mouse_y() - win_oy();
            if (my >= win_h() - 74) {
              if (my < win_h() - 18) {
                if (g_err_line > 0) {
                  jump_line(g_err_line);
                }
              }
            }
          }
        }
      }
      g_prev = mb;
      k = ev_key();
      while (k) {
        handle_key(k);
        k = ev_key();
      }
      ch = ev_text();
      while (ch) {
        handle_text(ch);
        ch = ev_text();
      }
    }
    fillrgb(0, 20, win_w(), 48, 0x00101828);
    fillrgb(8, 24, 48, 18, 0x00406080);
    text(14, 27, "Open", 0x00FFFFFF);
    fillrgb(60, 24, 48, 18, 0x00406080);
    if (g_browse) {
      text(68, 27, "Back", 0x00FFFFFF);
    } else {
      text(68, 27, "Save", 0x00FFFFFF);
    }
    fillrgb(112, 24, 64, 18, 0x00406080);
    text(118, 27, "SaveAs", 0x00FFFFFF);
    fillrgb(180, 24, 72, 18, 0x00406080);
    text(186, 27, "Compile", 0x00FFFFFF);
    fillrgb(256, 24, 40, 18, 0x00406080);
    text(264, 27, "Go", 0x00FFFFFF);
    fillrgb(304, 24, 48, 18, 0x00406080);
    text(310, 27, "Make", 0x00FFFFFF);
    copy_show(g_path, 0);
    text(360, 27, g_show, 0x00A0C8E0);
    if (g_browse) {
      if (loadb(g_dir) == 0) {
        text(8, 48, "/", 0x00FFE080);
      } else {
        copy_show(g_dir, g_hscroll);
        text(8, 48, g_show, 0x00FFE080);
      }
      if (g_saveas) {
        text(8, 68, "Save As ??? enter folder, open file or :w name", 0x00E0C080);
      } else {
        text(8, 68, "Open file", 0x0080A0B0);
      }
      y = 88;
      g_nshow = 0;
      list_w = win_w() - 28;
      fillrgb(6, y, list_w - 4, 16, 0x00304050);
      text(10, y + 1, "..", 0x00FFFFFF);
      g_nshow = 1;
      y = y + 18;
      view = (win_h() - 88 - 18) / 18;
      i = g_scroll;
      while (i < g_ntotal) {
        if (g_nshow > view) {
          i = g_ntotal;
        } else {
          if (entry_at(i)) {
            fillrgb(6, y, list_w - 4, 16, 0x00203040);
            text(10, y + 1, g_ent, 0x00FFFFFF);
            g_nshow = g_nshow + 1;
            y = y + 18;
          }
          i = i + 1;
        }
      }
    } else {
      fillrgb(0, 48, win_w(), 20, 0x00101828);
      text(8, 48, "Esc Normal  i insert  :w :q  /find  hjkl  dd x gg G", 0x00607080);
      draw_text_view();
    }
    draw_status();
    win_end();
    wait(1);
  }
}
