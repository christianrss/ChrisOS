int g_slot;
int g_y;
int g_kind;

int class_of() {
  int c;
  c = drv_pci(0, g_slot, 0, 8);
  c = c >> 16;
  return c & 65535;
}

int note(int code) {
  if (code == 0x0101) {
    text(8, g_y, "ide ata", 0x00E0E0E0);
    return 1;
  }
  if (code == 0x0106) {
    text(8, g_y, "ahci sata", 0x00E0E0E0);
    return 1;
  }
  if (code == 0x0108) {
    text(8, g_y, "nvme", 0x00E0E0E0);
    return 1;
  }
  if (code == 0x0C03) {
    text(8, g_y, "usb", 0x00E0E0E0);
    return 1;
  }
  if (code == 0x0300) {
    text(8, g_y, "display", 0x00E0E0E0);
    return 1;
  }
  if (code == 0x0200) {
    text(8, g_y, "net", 0x00E0E0E0);
    return 1;
  }
  if (code == 0x0401) {
    text(8, g_y, "audio", 0x00E0E0E0);
    return 1;
  }
  return 0;
}

int probe_bar() {
  int id;
  int dev;
  int win;
  int ver;
  id = drv_pci(0, g_slot, 0, 0);
  dev = id >> 16;
  dev = dev & 65535;
  if ((id & 65535) == 0x1AF4) {
    if (dev == 0x1050) {
      text(120, g_y, "virtio-gpu", 0x00A0FFA0);
    }
    if (dev == 0x1001) {
      text(120, g_y, "virtio-blk", 0x00A0FFA0);
    }
    if (dev == 0x1042) {
      text(120, g_y, "virtio-blk", 0x00A0FFA0);
    }
    if (dev == 0x1000) {
      text(120, g_y, "virtio-net", 0x00A0FFA0);
    }
    return 0;
  }
  if (g_kind == 0x0108) {
    win = bar_map(0, g_slot, 0, 0);
    if (win >= 0) {
      ver = mmio_r32(win, 8);
      if (ver > 0) {
        text(120, g_y, "nvme bar", 0x00A0FFA0);
      }
    }
  }
  if (g_kind == 0x0106) {
    win = bar_map(0, g_slot, 0, 5);
    if (win >= 0) {
      ver = mmio_r32(win, 0x10);
      if (ver > 0) {
        text(120, g_y, "ahci bar", 0x00A0FFA0);
      }
    }
  }
  return 0;
}

void main() {
  int id;
  int n;
  viewport(280, 200);
  text(8, 8, "pci", 0x00FFFFFF);
  g_y = 28;
  g_slot = 0;
  n = 0;
  while (g_slot < 32) {
    id = drv_pci(0, g_slot, 0, 0);
    if ((id & 65535) != 65535) {
      g_kind = class_of();
      if (note(g_kind) == 1) {
        probe_bar();
        g_y = g_y + 16;
        n = n + 1;
      }
    }
    g_slot = g_slot + 1;
  }
  if (n == 0) {
    text(8, 28, "none", 0x00FF8080);
  }
  while (1) {
    wait(200);
  }
}
