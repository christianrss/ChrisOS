void main() {
  int fd;
  int c;
  char req[64];
  char rep[48];
  int i;
  viewport(200, 80);
  text(8, 8, "http", 0x00FFFFFF);
  fd = sock_listen(8080);
  while (1) {
    c = sock_accept(fd);
    if (c > 0) {
      i = sock_recv(c, req, 60);
      rep[0] = 72;
      rep[1] = 84;
      rep[2] = 84;
      rep[3] = 80;
      rep[4] = 47;
      rep[5] = 49;
      rep[6] = 46;
      rep[7] = 48;
      rep[8] = 32;
      rep[9] = 50;
      rep[10] = 48;
      rep[11] = 48;
      rep[12] = 32;
      rep[13] = 79;
      rep[14] = 75;
      rep[15] = 13;
      rep[16] = 10;
      rep[17] = 13;
      rep[18] = 10;
      rep[19] = 104;
      rep[20] = 105;
      rep[21] = 0;
      sock_send(c, rep, 21);
      sock_close(c);
    }
  }
}
