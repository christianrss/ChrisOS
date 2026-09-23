int g_src;
int g_len;
int g_out;
int g_pc;

int fnv_byte(int h, int b) {
  h = h ^ b;
  h = h * 16777619;
  h = h & 0xFFFFFFFF;
  return h;
}

void emit(int b) {
  storeb(g_out + 16 + g_pc, b);
  g_pc = g_pc + 1;
}

void emit32(int v) {
  emit(v & 255);
  emit((v / 256) & 255);
  emit((v / 65536) & 255);
  emit((v / 16777216) & 255);
}

int skip_ws(int i) {
  int c;
  while (i < g_len) {
    c = loadb(g_src + i);
    if (c == 32) {
      i = i + 1;
    }
    if (c == 10) {
      i = i + 1;
    }
    if (c == 13) {
      i = i + 1;
    }
    if (c == 9) {
      i = i + 1;
    }
    if (c != 32) {
      if (c != 10) {
        if (c != 13) {
          if (c != 9) {
            return i;
          }
        }
      }
    }
  }
  return i;
}

int match_at(int i, int a, int b, int c, int d, int e, int f) {
  if (i + 6 > g_len) {
    return 0;
  }
  if (loadb(g_src + i) != a) {
    return 0;
  }
  if (loadb(g_src + i + 1) != b) {
    return 0;
  }
  if (loadb(g_src + i + 2) != c) {
    return 0;
  }
  if (loadb(g_src + i + 3) != d) {
    return 0;
  }
  if (loadb(g_src + i + 4) != e) {
    return 0;
  }
  if (loadb(g_src + i + 5) != f) {
    return 0;
  }
  return 1;
}

int read_num(int i, int *val) {
  int n;
  int c;
  n = 0;
  i = skip_ws(i);
  c = loadb(g_src + i);
  while (c >= 48) {
    if (c > 57) {
      *val = n;
      return i;
    }
    n = n * 10 + (c - 48);
    i = i + 1;
    c = loadb(g_src + i);
  }
  *val = n;
  return i;
}

void write_clv(int retv) {
  int h;
  int i;
  int fd;
  char path[16];
  g_pc = 0;
  emit(1);
  emit32(retv);
  emit(8);
  storeb(g_out, 67);
  storeb(g_out + 1, 76);
  storeb(g_out + 2, 86);
  storeb(g_out + 3, 77);
  storeb(g_out + 4, 1);
  storeb(g_out + 5, 0);
  storeb(g_out + 6, 0);
  storeb(g_out + 7, 0);
  storeb(g_out + 8, g_pc & 255);
  storeb(g_out + 9, (g_pc / 256) & 255);
  storeb(g_out + 10, 0);
  storeb(g_out + 11, 0);
  h = 0x811C9DC5;
  i = 0;
  while (i < g_pc) {
    h = fnv_byte(h, loadb(g_out + 16 + i));
    i = i + 1;
  }
  storeb(g_out + 12, h & 255);
  storeb(g_out + 13, (h / 256) & 255);
  storeb(g_out + 14, (h / 65536) & 255);
  storeb(g_out + 15, (h / 16777216) & 255);
  path[0] = 65;
  path[1] = 80;
  path[2] = 80;
  path[3] = 83;
  path[4] = 47;
  path[5] = 67;
  path[6] = 67;
  path[7] = 47;
  path[8] = 79;
  path[9] = 85;
  path[10] = 84;
  path[11] = 46;
  path[12] = 67;
  path[13] = 76;
  path[14] = 86;
  path[15] = 0;
  fd = fopen(path);
  if (fd < 0) {
    return;
  }
  fwrite(fd, g_out, 16 + g_pc);
  fclose(fd);
}

void main() {
  int fd;
  int i;
  int n;
  int retv;
  char path[16];
  viewport(220, 80);
  text(8, 8, "cc", 0x00FFFFFF);
  g_src = malloc(4096);
  g_out = malloc(64);
  g_len = 0;
  path[0] = 65;
  path[1] = 80;
  path[2] = 80;
  path[3] = 83;
  path[4] = 47;
  path[5] = 67;
  path[6] = 67;
  path[7] = 47;
  path[8] = 73;
  path[9] = 78;
  path[10] = 46;
  path[11] = 67;
  path[12] = 67;
  path[13] = 0;
  fd = fopen(path);
  if (fd >= 0) {
    g_len = fread(fd, g_src, 4000);
    fclose(fd);
  }
  if (g_len < 0) {
    g_len = 0;
  }
  retv = 0;
  i = 0;
  while (i + 6 < g_len) {
    if (match_at(i, 114, 101, 116, 117, 114, 110)) {
      i = read_num(i + 6, &retv);
      i = g_len;
    }
    i = i + 1;
  }
  write_clv(retv);
  while (1) {
  }
}
