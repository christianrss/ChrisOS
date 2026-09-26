void main() {
  int s;
  int p;
  int o;
  int i;
  int fd;
  viewport(200, 80);
  text(8, 8, "tls", 0x00FFFFFF);
  s = malloc(32);
  p = malloc(32);
  o = malloc(32);
  storeb(p, 9);
  i = 1;
  while (i < 32) {
    storeb(p + i, 0);
    storeb(s + i, rng_u32());
    i = i + 1;
  }
  storeb(s, rng_u32());
  x25519(o, s, p);
  fd = sock_connect(167773186, 443);
  sock_send(fd, o, 32);
  while (1) {
  }
}
