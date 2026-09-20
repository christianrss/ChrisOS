int ui_hit(int x, int y, int w, int h) {
  int mx;
  int my;
  mx = mouse_x() - win_ox();
  my = mouse_y() - win_oy();
  if (mx >= x) {
    if (mx < x + w) {
      if (my >= y) {
        if (my < y + h) {
          return 1;
        }
      }
    }
  }
  return 0;
}
