void main() {
  int ip;
  int fd;
  char req[40];
  char buf[128];
  int n;
  int i;
  viewport(320, 160);
  text(8, 8, "browser", 0x00FFFFFF);
  ip = dns_lookup("example.com");
  fd = sock_connect(ip, 80);
  req[0] = 71;
  req[1] = 69;
  req[2] = 84;
  req[3] = 32;
  req[4] = 47;
  req[5] = 32;
  req[6] = 72;
  req[7] = 84;
  req[8] = 84;
  req[9] = 80;
  req[10] = 47;
  req[11] = 49;
  req[12] = 46;
  req[13] = 48;
  req[14] = 13;
  req[15] = 10;
  req[16] = 13;
  req[17] = 10;
  req[18] = 0;
  i = 0;
  while (i < 40) {
    n = sock_send(fd, req, 18);
    if (n > 0) {
      i = 40;
    }
    i = i + 1;
  }
  n = sock_recv(fd, buf, 120);
  if (n > 0) {
    storeb(buf + n, 0);
    text(8, 28, buf, 0x00E0E0E0);
  }
  sock_close(fd);
  while (1) {
  }
}
