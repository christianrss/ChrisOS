int g_prev;
int g_layout;

void main() {
  int live;
  int cur;
  g_prev = 0;
  g_layout = kb_get();
  while (1) {
    live = win_begin(420, 220, "Prefs");
    if (live == 0) {
      return;
    }
    cur = kb_get();
    g_layout = cur;
    text(16, 48, "Keyboard layout", 0x001C1814);
    text(16, 68, "Choose US or ABNT-2 (Brazil).", 0x00504038);
    if (g_layout == 1) {
      fillrgb(16, 100, 160, 28, 0x00406080);
      text(36, 108, "US QWERTY", 0x00FFFFFF);
      fillrgb(196, 100, 160, 28, 0x00008050);
      text(220, 108, "ABNT-2", 0x00FFFFFF);
      text(16, 150, "Active: ABNT-2", 0x00603818);
    } else {
      fillrgb(16, 100, 160, 28, 0x00008050);
      text(36, 108, "US QWERTY", 0x00FFFFFF);
      fillrgb(196, 100, 160, 28, 0x00406080);
      text(220, 108, "ABNT-2", 0x00FFFFFF);
      text(16, 150, "Active: US", 0x00603818);
    }
    text(16, 178, "Saved to SYS/KB.CFG", 0x00504038);
    text(16, win_h() - 28, "layout persists across reboot", 0x00504038);
    if (win_click(16, 100, 160, 28)) {
      kb_layout(0);
      g_layout = 0;
    }
    if (win_click(196, 100, 160, 28)) {
      mkdir("SYS");
      kb_layout(1);
      g_layout = 1;
    }
    win_end();
    wait(1);
  }
}
