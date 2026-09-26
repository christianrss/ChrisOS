int bmp_w(int p) {
  int w;
  w = loadb(p + 18);
  w = w + loadb(p + 19) * 256;
  return w;
}

int bmp_h(int p) {
  int h;
  h = loadb(p + 22);
  h = h + loadb(p + 23) * 256;
  return h;
}

int bmp_off(int p) {
  return loadb(p + 10) + loadb(p + 11) * 256;
}
