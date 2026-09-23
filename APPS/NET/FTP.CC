void main() {
  int cmd;
  int data;
  int c;
  char msg[16];
  viewport(200, 80);
  text(8, 8, "ftp", 0x00FFFFFF);
  cmd = sock_listen(21);
  data = sock_listen(20);
  msg[0] = 50;
  msg[1] = 50;
  msg[2] = 48;
  msg[3] = 32;
  msg[4] = 67;
  msg[5] = 104;
  msg[6] = 114;
  msg[7] = 105;
  msg[8] = 115;
  msg[9] = 13;
  msg[10] = 10;
  msg[11] = 0;
  while (1) {
    c = sock_accept(cmd);
    if (c > 0) {
      sock_send(c, msg, 11);
      sock_close(c);
    }
    c = sock_accept(data);
    if (c > 0) {
      sock_send(c, msg, 11);
      sock_close(c);
    }
  }
}
