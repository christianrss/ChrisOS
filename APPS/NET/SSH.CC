void main() {
  int fd;
  char ban[24];
  int i;
  viewport(200, 80);
  text(8, 8, "ssh", 0x00FFFFFF);
  ban[0] = 83;
  ban[1] = 83;
  ban[2] = 72;
  ban[3] = 45;
  ban[4] = 50;
  ban[5] = 46;
  ban[6] = 48;
  ban[7] = 45;
  ban[8] = 67;
  ban[9] = 104;
  ban[10] = 114;
  ban[11] = 105;
  ban[12] = 115;
  ban[13] = 13;
  ban[14] = 10;
  ban[15] = 0;
  fd = sock_connect(167773186, 22);
  i = 0;
  while (i < 8) {
    if (sock_send(fd, ban, 15) > 0) {
      i = 8;
    }
    i = i + 1;
  }
  while (1) {
  }
}
