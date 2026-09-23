int g_bus;
int g_slot;
int g_cwin;
int g_coff;
int g_nwin;
int g_noff;
int g_mult;
int g_q;
int g_cmd;
int g_fb;
int g_qlo;
int g_qhi;
int g_clo;
int g_chi;
int g_flo;
int g_fhi;
int g_idx;
int g_qnote;

int find_gpu() {
  int id;
  int dev;
  g_bus = 0;
  g_slot = 0;
  while (g_slot < 32) {
    id = drv_pci(g_bus, g_slot, 0, 0);
    if ((id & 65535) == 0x1AF4) {
      dev = id >> 16;
      dev = dev & 65535;
      if (dev == 0x1050) {
        return 1;
      }
    }
    g_slot = g_slot + 1;
  }
  return 0;
}

int enable_dev() {
  int cmd;
  cmd = drv_pci(g_bus, g_slot, 0, 4);
  cmd = cmd | 6;
  return pci_write(g_bus, g_slot, 0, 4, cmd);
}

int take_caps() {
  int off;
  int cap;
  int vndr;
  int next;
  int typ;
  int bar;
  int cfg;
  int win;
  off = drv_pci(g_bus, g_slot, 0, 0x34);
  off = off & 255;
  g_cwin = 0 - 1;
  g_nwin = 0 - 1;
  g_mult = 0;
  while (off > 0) {
    cap = drv_pci(g_bus, g_slot, 0, off);
    vndr = cap & 255;
    next = cap >> 8;
    next = next & 255;
    typ = cap >> 24;
    typ = typ & 255;
    if (vndr == 9) {
      bar = drv_pci(g_bus, g_slot, 0, off + 4);
      bar = bar & 255;
      cfg = drv_pci(g_bus, g_slot, 0, off + 8);
      win = bar_map(g_bus, g_slot, 0, bar);
      if (typ == 1) {
        g_cwin = win;
        g_coff = cfg;
      }
      if (typ == 2) {
        g_nwin = win;
        g_noff = cfg;
        g_mult = drv_pci(g_bus, g_slot, 0, off + 16);
      }
    }
    off = next;
  }
  if (g_cwin < 0) {
    return 0;
  }
  if (g_nwin < 0) {
    return 0;
  }
  return 1;
}

int dev_status(int v) {
  return mmio_w8(g_cwin, g_coff + 20, v);
}

int features_ok() {
  int st;
  mmio_w32(g_cwin, g_coff, 1);
  mmio_w32(g_cwin, g_coff + 8, 1);
  mmio_w32(g_cwin, g_coff + 12, 1);
  dev_status(11);
  st = mmio_r8(g_cwin, g_coff + 20);
  st = st & 8;
  if (st == 0) {
    return 0;
  }
  return 1;
}

int setup_queue() {
  int ulo;
  g_q = dma_alloc(1);
  g_cmd = dma_alloc(1);
  g_fb = dma_alloc(64);
  if (g_q < 0) {
    return 0;
  }
  if (g_cmd < 0) {
    return 0;
  }
  if (g_fb < 0) {
    return 0;
  }
  g_qlo = dma_lo(g_q);
  g_qhi = dma_hi(g_q);
  g_clo = dma_lo(g_cmd);
  g_chi = dma_hi(g_cmd);
  g_flo = dma_lo(g_fb);
  g_fhi = dma_hi(g_fb);
  mmio_w16(g_cwin, g_coff + 22, 0);
  mmio_w16(g_cwin, g_coff + 24, 4);
  mmio_w32(g_cwin, g_coff + 32, g_qlo);
  mmio_w32(g_cwin, g_coff + 36, g_qhi);
  mmio_w32(g_cwin, g_coff + 40, g_qlo + 64);
  mmio_w32(g_cwin, g_coff + 44, g_qhi);
  ulo = g_qlo + 2048;
  mmio_w32(g_cwin, g_coff + 48, ulo);
  mmio_w32(g_cwin, g_coff + 52, g_qhi);
  mmio_w16(g_cwin, g_coff + 28, 1);
  g_qnote = mmio_r16(g_cwin, g_coff + 30);
  g_noff = g_noff + g_qnote * g_mult;
  dev_status(15);
  return 1;
}

int clear_cmd() {
  int i;
  i = 0;
  while (i < 16) {
    dma_w32(g_cmd, i * 4, 0);
    dma_w32(g_cmd, 256 + i * 4, 0);
    i = i + 1;
  }
  return 0;
}

int kick(int len) {
  int spins;
  int used;
  dma_w32(g_q, 0, g_clo);
  dma_w32(g_q, 4, g_chi);
  dma_w32(g_q, 8, len);
  dma_w32(g_q, 12, 65537);
  dma_w32(g_q, 16, g_clo + 256);
  dma_w32(g_q, 20, g_chi);
  dma_w32(g_q, 24, 64);
  dma_w32(g_q, 28, 2);
  g_idx = g_idx + 1;
  dma_w32(g_q, 64, g_idx * 65536);
  mmio_w16(g_nwin, g_noff, 0);
  spins = 0;
  while (spins < 100000) {
    used = dma_r32(g_q, 2048);
    used = used >> 16;
    used = used & 65535;
    if (used == g_idx) {
      return dma_r32(g_cmd, 256);
    }
    spins = spins + 1;
  }
  return 0;
}

int cmd4(int at, int v) {
  return dma_w32(g_cmd, at, v);
}

int send_create() {
  clear_cmd();
  cmd4(0, 0x0101);
  cmd4(24, 1);
  cmd4(28, 1);
  cmd4(32, 320);
  cmd4(36, 200);
  return kick(40);
}

int send_attach() {
  clear_cmd();
  cmd4(0, 0x0106);
  cmd4(24, 1);
  cmd4(28, 1);
  cmd4(32, g_flo);
  cmd4(36, g_fhi);
  cmd4(40, 256000);
  return kick(48);
}

int send_scanout() {
  clear_cmd();
  cmd4(0, 0x0103);
  cmd4(32, 320);
  cmd4(36, 200);
  cmd4(44, 1);
  return kick(48);
}

int gpu_init() {
  int resp;
  g_idx = 0;
  if (find_gpu() == 0) {
    return 0;
  }
  enable_dev();
  if (take_caps() == 0) {
    return 0;
  }
  dev_status(0);
  dev_status(1);
  dev_status(3);
  if (features_ok() == 0) {
    return 0;
  }
  if (setup_queue() == 0) {
    return 0;
  }
  resp = send_create();
  if (resp != 0x1100) {
    return 0;
  }
  resp = send_attach();
  if (resp != 0x1100) {
    return 0;
  }
  resp = send_scanout();
  if (resp != 0x1100) {
    return 0;
  }
  if (gpu_arm(g_q, g_cmd, g_fb, 320, 200, g_nwin, g_noff) != 0) {
    return 0;
  }
  return 1;
}

void main() {
  viewport(220, 40);
  if (gpu_init() == 1) {
    text(4, 12, "virtio-gpu", 0x00E0FFE0);
  } else {
    text(4, 12, "no gpu", 0x00FF8080);
  }
  while (1) {
    wait(100);
  }
}
