char g_name[64];
int g_prev;

void main() {
  int live;
  int n;
  int i;
  int y;
  int mb;
  int my;
  int row;
  g_prev = 0;
  while (1) {
    live = win_begin(420, 300, "Tasks");
    if (live == 0) {
      return;
    }
    n = app_count();
    text(8, 24, "click to close", 0x0080A0B0);
    i = 0;
    y = 44;
    while (i < n) {
      if (i < 12) {
        fillrgb(6, y, 408, 16, 0x00203040);
        if (app_info(i, g_name)) {
          text(10, y + 1, g_name, 0x00FFFFFF);
        }
        y = y + 18;
      }
      i = i + 1;
    }
    mb = mouse_btn();
    if (mb) {
      if (g_prev == 0) {
        my = mouse_y() - win_oy();
        if (my >= 44) {
          row = (my - 44) / 18;
          if (row >= 0) {
            if (row < n) {
              if (row < 12) {
                app_kill(row);
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
