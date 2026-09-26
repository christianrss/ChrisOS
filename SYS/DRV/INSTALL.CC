void main() {
  int i;
  int n;
  int k;
  int f;
  int y;
  viewport(280, 200);
  text(4, 4, "Install ChrisOS", 0x00E0FFE0);
  n = disk_count();
  i = 0;
  y = 20;
  while (i < n) {
    k = disk_kind(i);
    f = disk_flags(i);
    if (k == 6) {
      i = i + 1;
    } else {
      if (k == 1) {
        text(4, y, "ATA", 0x00E0E0E0);
      }
      if (k == 2) {
        text(4, y, "AHCI", 0x00E0E0E0);
      }
      if (k == 3) {
        text(4, y, "NVMe", 0x00E0E0E0);
      }
      if (k == 4) {
        text(4, y, "VirtIO", 0x00E0E0E0);
      }
      if (k == 5) {
        text(4, y, "USB", 0x00E0E0E0);
      }
      if (f != 0) {
        text(80, y, "SYSTEM", 0x00FF8080);
      }
      y = y + 14;
      i = i + 1;
    }
  }
  text(4, 160, "keys 1-9 pick target", 0x00C0C0C0);
  while (1) {
    if (key(2) == 1) {
      if (disk_install(1) == 0) {
        text(4, 176, "installed", 0x00E0FFE0);
      } else {
        text(4, 176, "refused", 0x00FF8080);
      }
    }
    if (key(3) == 1) {
      if (disk_install(2) == 0) {
        text(4, 176, "installed", 0x00E0FFE0);
      } else {
        text(4, 176, "refused", 0x00FF8080);
      }
    }
    if (key(4) == 1) {
      if (disk_install(3) == 0) {
        text(4, 176, "installed", 0x00E0FFE0);
      } else {
        text(4, 176, "refused", 0x00FF8080);
      }
    }
    wait(80);
  }
}
