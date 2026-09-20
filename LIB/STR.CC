int strlen(int s) {
  int n;
  n = 0;
  while (loadb(s + n) != 0) {
    n = n + 1;
  }
  return n;
}

int strcpy(int dst, int src) {
  int i;
  i = 0;
  while (1) {
    storeb(dst + i, loadb(src + i));
    if (loadb(src + i) == 0) {
      return dst;
    }
    i = i + 1;
  }
}
