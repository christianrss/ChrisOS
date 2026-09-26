void main() {
  int sec;
  int buf;
  viewport(300, 80);
  text(8, 8, "usb stick", 0x00FFFFFF);
  sec = disk_sectors();
  buf = malloc(512);
  if (buf == 0) {
    text(8, 28, "no mem", 0x00FF8080);
  } else {
    storeb(buf + 510, 0x55);
    storeb(buf + 511, 0xAA);
    storeb(buf + 446, 0x80);
    storeb(buf + 450, 0x83);
    text(8, 28, "mbr built", 0x00A0FFA0);
    text(8, 48, "lba0 locked", 0x00FFC080);
  }
  if (sec == 0) {
    text(160, 28, "no disk", 0x00FF8080);
  }
  while (1) {
    wait(200);
  }
}
