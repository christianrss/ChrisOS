void main() {
  int n;
  viewport(280, 48);
  n = disk_count();
  if (n < 2) {
    text(4, 16, "no target", 0x00FF8080);
  } else if (disk_install(1) == 0) {
    text(4, 16, "installed", 0x00E0FFE0);
  } else {
    text(4, 16, "install fail", 0x00FF8080);
  }
  while (1) {
    wait(100);
  }
}
