char g_name[64];
char g_num[16];
int g_prev;
int g_sel;
int g_scroll;

void task_num(int value) {
  int i;
  int d;
  int n;
  char tmp[16];
  if (value < 0) {
    value = 0;
  }
  if (value == 0) {
    storeb(g_num, 48);
    storeb(g_num + 1, 0);
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
    storeb(g_num + n, loadb(tmp + i));
    n = n + 1;
  }
  storeb(g_num + n, 0);
}

void main() {
  int live;
  int n;
  int i;
  int y;
  int mb;
  int my;
  int row;
  int view;
  int max_off;
  int list_h;
  int list_w;
  int k;
  g_prev = 0;
  g_sel = 0;
  g_scroll = 0;
  while (1) {
    live = win_begin(480, 340, "Tasks");
    if (live == 0) {
      return;
    }
    n = app_count();
    text(8, 24, "Heap used/free KB", 0x0080A0B0);
    task_num(sys_heap_used_kb());
    text(180, 24, g_num, 0x00FFFFFF);
    task_num(sys_heap_free_kb());
    text(270, 24, g_num, 0x00FFFFFF);
    text(8, 40, "PMM free pages", 0x0080A0B0);
    task_num(sys_pmm_free_pages());
    text(180, 40, g_num, 0x00FFFFFF);
    text(8, 56, "Frame p50/p95 ms", 0x0080A0B0);
    task_num(sys_frame_p50());
    text(180, 56, g_num, 0x00FFFFFF);
    task_num(sys_frame_p95());
    text(270, 56, g_num, 0x00FFFFFF);
    text(8, 72, "click raise   Close kills", 0x0080A0B0);
    fillrgb(win_w() - 80, 24, 56, 18, 0x00C02828);
    text(win_w() - 70, 27, "Close", 0x00FFFFFF);
    list_w = win_w() - 28;
    list_h = win_h() - 108 - 18;
    view = list_h / 18;
    if (view < 1) {
      view = 1;
    }
    if (g_sel < 0) {
      g_sel = 0;
    }
    if (g_sel >= n) {
      if (n > 0) {
        g_sel = n - 1;
      } else {
        g_sel = 0;
      }
    }
    if (g_sel < g_scroll) {
      g_scroll = g_sel;
    }
    if (g_sel >= g_scroll + view) {
      g_scroll = g_sel - view + 1;
    }
    max_off = n - view;
    if (max_off < 0) {
      max_off = 0;
    }
    g_scroll = win_vscroll(win_w() - 18, 108, list_h, max_off, view, g_scroll);
    i = g_scroll;
    y = 108;
    row = 0;
    while (i < n) {
      if (row >= view) {
        i = n;
      } else {
        if (i == g_sel) {
          fillrgb(6, y, list_w - 4, 16, 0x00406080);
        } else {
          fillrgb(6, y, list_w - 4, 16, 0x00203040);
        }
        if (app_info(i, g_name)) {
          text(10, y + 1, g_name, 0x00FFFFFF);
        }
        y = y + 18;
        row = row + 1;
        i = i + 1;
      }
    }
    k = ev_key();
    while (k) {
      if (k == 7) {
        if (g_sel > 0) {
          g_sel = g_sel - 1;
        }
      }
      if (k == 8) {
        if (g_sel + 1 < n) {
          g_sel = g_sel + 1;
        }
      }
      if (k == 3) {
        app_raise(g_sel);
      }
      k = ev_key();
    }
    if (win_click(win_w() - 80, 24, 56, 18)) {
      if (n > 0) {
        app_kill(g_sel);
      }
    }
    mb = mouse_btn();
    if (mb) {
      if (g_prev == 0) {
        my = mouse_y() - win_oy();
        if (my >= 108) {
          if (my < 108 + list_h) {
            row = (my - 108) / 18;
            i = g_scroll + row;
            if (i >= 0) {
              if (i < n) {
                g_sel = i;
                app_raise(i);
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
