void main() {
  int smtp;
  int pop;
  int c;
  char hi[12];
  char ok[8];
  viewport(200, 80);
  text(8, 8, "mail", 0x00FFFFFF);
  smtp = sock_listen(25);
  pop = sock_listen(110);
  hi[0] = 50;
  hi[1] = 50;
  hi[2] = 48;
  hi[3] = 32;
  hi[4] = 104;
  hi[5] = 105;
  hi[6] = 13;
  hi[7] = 10;
  hi[8] = 0;
  ok[0] = 43;
  ok[1] = 79;
  ok[2] = 75;
  ok[3] = 13;
  ok[4] = 10;
  ok[5] = 0;
  while (1) {
    c = sock_accept(smtp);
    if (c > 0) {
      sock_send(c, hi, 8);
      sock_close(c);
    }
    c = sock_accept(pop);
    if (c > 0) {
      sock_send(c, ok, 5);
      sock_close(c);
    }
  }
}
