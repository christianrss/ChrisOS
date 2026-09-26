int yes_arg() {
  int buf;
  int n;
  buf = malloc(16);
  n = app_arg(buf);
  if (n < 1) {
    return 0;
  }
  if (loadb(buf) != 89) {
    return 0;
  }
  if (loadb(buf + 1) != 69) {
    return 0;
  }
  if (loadb(buf + 2) != 83) {
    return 0;
  }
  return 1;
}

void main() {
  int sec;
  int rc;
  viewport(280, 64);
  sec = disk_sectors();
  text(8, 8, "format", 0x00FFFFFF);
  if (yes_arg() == 0) {
    text(8, 28, "arg YES", 0x00FFC080);
  } else {
    rc = disk_format();
    if (rc == 0) {
      text(8, 28, "cfs ok", 0x00A0FFA0);
    } else {
      text(8, 28, "cfs fail", 0x00FF8080);
    }
  }
  if (sec == 0) {
    text(120, 28, "no disk", 0x00FF8080);
  }
  while (1) {
    wait(200);
  }
}
