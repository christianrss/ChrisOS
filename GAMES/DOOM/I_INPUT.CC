/* Keyboard → D_PostEvent. ENGINE.LST only (needs d_event.c).
 * Scancodes map to doomkeys.h values (vanilla AT mapping). */

struct dg_event {
  int type;
  int data1;
  int data2;
  int data3;
  int data4;
};

void D_PostEvent(struct dg_event *ev);

void post_key(int down, int code) {
  struct dg_event ev;
  if (down) {
    ev.type = 0;
  } else {
    ev.type = 1;
  }
  ev.data1 = code;
  ev.data2 = code;
  ev.data3 = 0;
  ev.data4 = 0;
  D_PostEvent(&ev);
}

void poll_key(int sc, int code) {
  int down;
  down = key(sc);
  if (down) {
    if (key_was[sc] == 0) {
      post_key(1, code);
    }
  } else {
    if (key_was[sc]) {
      post_key(0, code);
    }
  }
  key_was[sc] = down;
}

void I_StartTic() {
  /* Esc / Enter / Tab / Backspace */
  poll_key(1, 27);
  poll_key(28, 13);
  poll_key(15, 9);
  poll_key(14, 127);
  /* Space = KEY_USE (0xa2), Ctrl = KEY_FIRE (0xa3), Shift = KEY_RSHIFT */
  poll_key(57, 162);
  poll_key(29, 163);
  poll_key(42, 182);
  poll_key(54, 182);
  /* Arrows */
  poll_key(72, 173);
  poll_key(80, 175);
  poll_key(75, 172);
  poll_key(77, 174);
  /* WASD + strafe alt */
  poll_key(17, 119);
  poll_key(31, 115);
  poll_key(30, 97);
  poll_key(32, 100);
  /* Numbers for weapons */
  poll_key(2, 49);
  poll_key(3, 50);
  poll_key(4, 51);
  poll_key(5, 52);
  poll_key(6, 53);
  poll_key(7, 54);
  poll_key(8, 55);
  /* Y/N for quit confirm */
  poll_key(21, 121);
  poll_key(49, 110);
}
