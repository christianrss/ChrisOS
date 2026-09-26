int g_doc;
int g_zoom;
int g_pan;
int g_undo;
int g_prev;

void main() {
  int x;
  int y;
  g_doc = malloc(64 * 64);
  g_zoom = 2;
  g_pan = 0;
  g_undo = 0;
  g_prev = 0;
  viewport(200, 160);
  x = 0;
  while (x < 64 * 64) {
    storeb(g_doc + x, 40);
    x = x + 1;
  }
  while (1) {
    fillrgb(0, 0, 200, 160, 0x00101820);
    y = 0;
    while (y < 64) {
      x = 0;
      while (x < 64) {
        fillrgb(g_pan + x * g_zoom, y * g_zoom, g_zoom, g_zoom,
                loadb(g_doc + y * 64 + x) * 0x010101);
        x = x + 1;
      }
      y = y + 1;
    }
    if (mouse_btn()) {
      if (g_prev == 0) {
        g_undo = loadb(g_doc);
        storeb(g_doc, 200);
      }
      g_prev = 1;
    }
    if (mouse_btn() == 0) {
      if (g_prev == 1) {
        storeb(g_doc, g_undo);
      }
      g_prev = 0;
    }
    text(8, 140, "paint", 0x00FFFFFF);
  }
}
