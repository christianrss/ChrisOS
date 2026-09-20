#include "LIB/STR.H"
#include "LIB/STR.CC"

void main() {
  char buf[128];
  int fd;
  int n;
  int i;
  int c;
  int len;
  viewport(320, 200);
  fd = fopen("SRC/HELLO.TXT");
  if (fd < 0) {
    clear(4);
    while (1) {
      if (key(1)) {
        return;
      }
      wait(1);
    }
  }
  n = fread(fd, buf, 127);
  fclose(fd);
  if (n < 0) {
    n = 0;
  }
  if (n > 127) {
    n = 127;
  }
  storeb(buf + n, 0);
  len = strlen(buf);
  while (1) {
    if (key(1)) {
      return;
    }
    clear(1);
    i = 0;
    while (i < len) {
      c = loadb(buf + i);
      rect(8 + i * 2, 70, 2, 10 + (c % 16), 14);
      i = i + 1;
    }
    wait(1);
  }
}
