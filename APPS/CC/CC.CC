int g_src;
int g_len;
int g_out;
int g_pc;
int g_i;
int g_c;
int g_nlen;
int g_name;
int g_num;
int g_names;
int g_fnames;
int g_fpc;
int g_ng;
int g_nf;
int g_fix;
int g_fixsp;
int g_entry;
int g_fd;
int g_path;
int g_b;
int g_op;
int g_k;
int g_j;
int g_slot;
int g_base;
int g_c2;
int g_skip;
int g_addr;
int g_save;
int g_rel;
int g_site;
int g_push;
int g_pop;
int g_bi;
int g_fnpc;
int g_argc;
int g_leave;
int g_main;
int g_kind;
int g_stop;
int g_more;
int g_mark;
int g_at;
int g_lo;
int g_hi;
int g_h;
int g_ok;
int g_ret;
int g_cfx;
int g_nc;
int g_pending;
int g_boot;
int g_mainpc;
int g_lnames;
int g_laddr;
int g_lwid;
int g_larr;
int g_nloc;
int g_locbase;
int g_nparam;
int g_scale;
int g_arr;
int g_bytes;
int g_sz;
int g_sbuf;
int g_poff;
int g_saddr;
int g_strinit;
int g_outpath;
int g_arg;
int g_ty;

int op() {
  storeb(g_out + 16 + g_pc, g_op);
  g_pc = g_pc + 1;
  return 0;
}

int eu() {
  g_b = g_num & 255;
  storeb(g_out + 16 + g_pc, g_b);
  g_pc = g_pc + 1;
  g_num = g_num >> 8;
  g_b = g_num & 255;
  storeb(g_out + 16 + g_pc, g_b);
  g_pc = g_pc + 1;
  g_num = g_num >> 8;
  g_b = g_num & 255;
  storeb(g_out + 16 + g_pc, g_b);
  g_pc = g_pc + 1;
  g_num = g_num >> 8;
  g_b = g_num & 255;
  storeb(g_out + 16 + g_pc, g_b);
  g_pc = g_pc + 1;
  return 0;
}

int epush() {
  g_op = 1;
  op();
  eu();
  return 0;
}

int st32() {
  g_b = g_num & 255;
  storeb(g_at, g_b);
  g_num = g_num >> 8;
  g_b = g_num & 255;
  storeb(g_at + 1, g_b);
  g_num = g_num >> 8;
  g_b = g_num & 255;
  storeb(g_at + 2, g_b);
  g_num = g_num >> 8;
  g_b = g_num & 255;
  storeb(g_at + 3, g_b);
  return 0;
}

int ld32() {
  g_ret = loadb(g_at);
  g_b = loadb(g_at + 1);
  g_b = g_b * 256;
  g_ret = g_ret + g_b;
  g_b = loadb(g_at + 2);
  g_b = g_b * 65536;
  g_ret = g_ret + g_b;
  g_b = loadb(g_at + 3);
  g_b = g_b * 16777216;
  g_ret = g_ret + g_b;
  return g_ret;
}

int stfix() {
  g_at = g_fix + g_fixsp;
  g_num = g_push;
  st32();
  g_fixsp = g_fixsp + 4;
  return 0;
}

int popfix() {
  g_fixsp = g_fixsp - 4;
  g_at = g_fix + g_fixsp;
  ld32();
  g_pop = g_ret;
  return g_pop;
}

int sws() {
  while (1) {
    if (g_i >= g_len) {
      g_c = 0;
      return 0;
    }
    g_c = loadb(g_src + g_i);
    g_skip = 0;
    if (g_c == 32) {
      g_skip = 1;
    }
    if (g_c == 9) {
      g_skip = 1;
    }
    if (g_c == 10) {
      g_skip = 1;
    }
    if (g_c == 13) {
      g_skip = 1;
    }
    if (g_skip == 0) {
      return 1;
    }
    g_i = g_i + 1;
  }
  return 0;
}

int is_id() {
  if (g_c >= 65) {
    if (g_c <= 90) {
      return 1;
    }
  }
  if (g_c >= 97) {
    if (g_c <= 122) {
      return 1;
    }
  }
  if (g_c == 95) {
    return 1;
  }
  if (g_nlen > 0) {
    if (g_c >= 48) {
      if (g_c <= 57) {
        return 1;
      }
    }
  }
  return 0;
}

int rdname() {
  sws();
  g_nlen = 0;
  g_k = 0;
  while (g_k < 16) {
    storeb(g_name + g_k, 0);
    g_k = g_k + 1;
  }
  while (1) {
    if (g_i >= g_len) {
      return 0;
    }
    g_c = loadb(g_src + g_i);
    if (is_id() == 0) {
      return 1;
    }
    if (g_nlen < 15) {
      storeb(g_name + g_nlen, g_c);
      g_nlen = g_nlen + 1;
    }
    g_i = g_i + 1;
  }
  return 1;
}

int hexdig() {
  g_ret = 0;
  if (g_c >= 48) {
    if (g_c <= 57) {
      g_b = g_c - 48;
      g_ret = 1;
    }
  }
  if (g_c >= 97) {
    if (g_c <= 102) {
      g_b = g_c - 87;
      g_ret = 1;
    }
  }
  if (g_c >= 65) {
    if (g_c <= 70) {
      g_b = g_c - 55;
      g_ret = 1;
    }
  }
  return g_ret;
}

int rdnum() {
  sws();
  g_num = 0;
  g_stop = 0;
  while (g_stop == 0) {
    if (g_i >= g_len) {
      g_stop = 1;
    }
    if (g_stop == 0) {
      g_c = loadb(g_src + g_i);
      if (g_c < 48) {
        g_stop = 1;
      }
      if (g_c > 57) {
        g_stop = 1;
      }
    }
    if (g_stop == 0) {
      g_num = g_num * 10;
      g_b = g_c - 48;
      g_num = g_num + g_b;
      g_i = g_i + 1;
    }
  }
  if (g_num == 0) {
    if (g_i < g_len) {
      g_c = loadb(g_src + g_i);
      if (g_c == 120) {
        g_i = g_i + 1;
        g_stop = 0;
        while (g_stop == 0) {
          if (g_i >= g_len) {
            g_stop = 1;
          }
          if (g_stop == 0) {
            g_c = loadb(g_src + g_i);
            hexdig();
            if (g_ret == 0) {
              g_stop = 1;
            }
          }
          if (g_stop == 0) {
            g_num = g_num * 16;
            g_num = g_num + g_b;
            g_i = g_i + 1;
          }
        }
      }
    }
  }
  return 1;
}

int is_if() {
  if (g_nlen != 2) {
    return 0;
  }
  if (loadb(g_name) != 105) {
    return 0;
  }
  if (loadb(g_name + 1) != 102) {
    return 0;
  }
  return 1;
}

int is_int() {
  if (g_nlen != 3) {
    return 0;
  }
  if (loadb(g_name) != 105) {
    return 0;
  }
  if (loadb(g_name + 1) != 110) {
    return 0;
  }
  if (loadb(g_name + 2) != 116) {
    return 0;
  }
  return 1;
}

int is_ret() {
  if (g_nlen != 6) {
    return 0;
  }
  if (loadb(g_name) != 114) {
    return 0;
  }
  if (loadb(g_name + 1) != 101) {
    return 0;
  }
  if (loadb(g_name + 2) != 116) {
    return 0;
  }
  if (loadb(g_name + 3) != 117) {
    return 0;
  }
  if (loadb(g_name + 4) != 114) {
    return 0;
  }
  if (loadb(g_name + 5) != 110) {
    return 0;
  }
  return 1;
}

int is_while() {
  if (g_nlen != 5) {
    return 0;
  }
  if (loadb(g_name) != 119) {
    return 0;
  }
  if (loadb(g_name + 1) != 104) {
    return 0;
  }
  if (loadb(g_name + 2) != 105) {
    return 0;
  }
  if (loadb(g_name + 3) != 108) {
    return 0;
  }
  if (loadb(g_name + 4) != 101) {
    return 0;
  }
  return 1;
}

int is_mainn() {
  if (g_nlen != 4) {
    return 0;
  }
  if (loadb(g_name) != 109) {
    return 0;
  }
  if (loadb(g_name + 1) != 97) {
    return 0;
  }
  if (loadb(g_name + 2) != 105) {
    return 0;
  }
  if (loadb(g_name + 3) != 110) {
    return 0;
  }
  return 1;
}

int is_loadb() {
  if (g_nlen != 5) {
    return 0;
  }
  if (loadb(g_name) != 108) {
    return 0;
  }
  if (loadb(g_name + 1) != 111) {
    return 0;
  }
  if (loadb(g_name + 2) != 97) {
    return 0;
  }
  if (loadb(g_name + 3) != 100) {
    return 0;
  }
  if (loadb(g_name + 4) != 98) {
    return 0;
  }
  return 1;
}

int is_storeb() {
  if (g_nlen != 6) {
    return 0;
  }
  if (loadb(g_name) != 115) {
    return 0;
  }
  if (loadb(g_name + 1) != 116) {
    return 0;
  }
  if (loadb(g_name + 2) != 111) {
    return 0;
  }
  if (loadb(g_name + 3) != 114) {
    return 0;
  }
  if (loadb(g_name + 4) != 101) {
    return 0;
  }
  if (loadb(g_name + 5) != 98) {
    return 0;
  }
  return 1;
}

int is_fopen() {
  if (g_nlen != 5) {
    return 0;
  }
  if (loadb(g_name) != 102) {
    return 0;
  }
  if (loadb(g_name + 1) != 111) {
    return 0;
  }
  if (loadb(g_name + 2) != 112) {
    return 0;
  }
  if (loadb(g_name + 3) != 101) {
    return 0;
  }
  if (loadb(g_name + 4) != 110) {
    return 0;
  }
  return 1;
}

int is_fclose() {
  if (g_nlen != 6) {
    return 0;
  }
  if (loadb(g_name) != 102) {
    return 0;
  }
  if (loadb(g_name + 1) != 99) {
    return 0;
  }
  if (loadb(g_name + 2) != 108) {
    return 0;
  }
  if (loadb(g_name + 3) != 111) {
    return 0;
  }
  if (loadb(g_name + 4) != 115) {
    return 0;
  }
  if (loadb(g_name + 5) != 101) {
    return 0;
  }
  return 1;
}

int is_fread() {
  if (g_nlen != 5) {
    return 0;
  }
  if (loadb(g_name) != 102) {
    return 0;
  }
  if (loadb(g_name + 1) != 114) {
    return 0;
  }
  if (loadb(g_name + 2) != 101) {
    return 0;
  }
  if (loadb(g_name + 3) != 97) {
    return 0;
  }
  if (loadb(g_name + 4) != 100) {
    return 0;
  }
  return 1;
}

int is_fwrite() {
  if (g_nlen != 6) {
    return 0;
  }
  if (loadb(g_name) != 102) {
    return 0;
  }
  if (loadb(g_name + 1) != 119) {
    return 0;
  }
  if (loadb(g_name + 2) != 114) {
    return 0;
  }
  if (loadb(g_name + 3) != 105) {
    return 0;
  }
  if (loadb(g_name + 4) != 116) {
    return 0;
  }
  if (loadb(g_name + 5) != 101) {
    return 0;
  }
  return 1;
}

int is_malloc() {
  if (g_nlen != 6) {
    return 0;
  }
  if (loadb(g_name) != 109) {
    return 0;
  }
  if (loadb(g_name + 1) != 97) {
    return 0;
  }
  if (loadb(g_name + 2) != 108) {
    return 0;
  }
  if (loadb(g_name + 3) != 108) {
    return 0;
  }
  if (loadb(g_name + 4) != 111) {
    return 0;
  }
  if (loadb(g_name + 5) != 99) {
    return 0;
  }
  return 1;
}

int is_void() {
  if (g_nlen != 4) {
    return 0;
  }
  if (loadb(g_name) != 118) {
    return 0;
  }
  if (loadb(g_name + 1) != 111) {
    return 0;
  }
  if (loadb(g_name + 2) != 105) {
    return 0;
  }
  if (loadb(g_name + 3) != 100) {
    return 0;
  }
  return 1;
}

int is_char() {
  if (g_nlen != 4) {
    return 0;
  }
  if (loadb(g_name) != 99) {
    return 0;
  }
  if (loadb(g_name + 1) != 104) {
    return 0;
  }
  if (loadb(g_name + 2) != 97) {
    return 0;
  }
  if (loadb(g_name + 3) != 114) {
    return 0;
  }
  return 1;
}

int is_else() {
  if (g_nlen != 4) {
    return 0;
  }
  if (loadb(g_name) != 101) {
    return 0;
  }
  if (loadb(g_name + 1) != 108) {
    return 0;
  }
  if (loadb(g_name + 2) != 115) {
    return 0;
  }
  if (loadb(g_name + 3) != 101) {
    return 0;
  }
  return 1;
}

int is_dispw() {
  if (g_nlen != 6) {
    return 0;
  }
  if (loadb(g_name) != 100) {
    return 0;
  }
  if (loadb(g_name + 1) != 105) {
    return 0;
  }
  if (loadb(g_name + 2) != 115) {
    return 0;
  }
  if (loadb(g_name + 3) != 112) {
    return 0;
  }
  if (loadb(g_name + 4) != 95) {
    return 0;
  }
  if (loadb(g_name + 5) != 119) {
    return 0;
  }
  return 1;
}

int is_disph() {
  if (g_nlen != 6) {
    return 0;
  }
  if (loadb(g_name) != 100) {
    return 0;
  }
  if (loadb(g_name + 1) != 105) {
    return 0;
  }
  if (loadb(g_name + 2) != 115) {
    return 0;
  }
  if (loadb(g_name + 3) != 112) {
    return 0;
  }
  if (loadb(g_name + 4) != 95) {
    return 0;
  }
  if (loadb(g_name + 5) != 104) {
    return 0;
  }
  return 1;
}

int is_view() {
  if (g_nlen != 8) {
    return 0;
  }
  if (loadb(g_name) != 118) {
    return 0;
  }
  if (loadb(g_name + 1) != 105) {
    return 0;
  }
  if (loadb(g_name + 2) != 101) {
    return 0;
  }
  if (loadb(g_name + 3) != 119) {
    return 0;
  }
  if (loadb(g_name + 4) != 112) {
    return 0;
  }
  if (loadb(g_name + 5) != 111) {
    return 0;
  }
  if (loadb(g_name + 6) != 114) {
    return 0;
  }
  if (loadb(g_name + 7) != 116) {
    return 0;
  }
  return 1;
}

int is_surf() {
  if (g_nlen != 10) {
    return 0;
  }
  if (loadb(g_name) != 115) {
    return 0;
  }
  if (loadb(g_name + 1) != 117) {
    return 0;
  }
  if (loadb(g_name + 2) != 114) {
    return 0;
  }
  if (loadb(g_name + 3) != 102) {
    return 0;
  }
  if (loadb(g_name + 4) != 95) {
    return 0;
  }
  if (loadb(g_name + 5) != 112) {
    return 0;
  }
  if (loadb(g_name + 6) != 108) {
    return 0;
  }
  if (loadb(g_name + 7) != 97) {
    return 0;
  }
  if (loadb(g_name + 8) != 99) {
    return 0;
  }
  if (loadb(g_name + 9) != 101) {
    return 0;
  }
  return 1;
}

int is_mx() {
  if (g_nlen != 7) {
    return 0;
  }
  if (loadb(g_name) != 109) {
    return 0;
  }
  if (loadb(g_name + 1) != 111) {
    return 0;
  }
  if (loadb(g_name + 2) != 117) {
    return 0;
  }
  if (loadb(g_name + 3) != 115) {
    return 0;
  }
  if (loadb(g_name + 4) != 101) {
    return 0;
  }
  if (loadb(g_name + 5) != 95) {
    return 0;
  }
  if (loadb(g_name + 6) != 120) {
    return 0;
  }
  return 1;
}

int is_my() {
  if (g_nlen != 7) {
    return 0;
  }
  if (loadb(g_name) != 109) {
    return 0;
  }
  if (loadb(g_name + 1) != 111) {
    return 0;
  }
  if (loadb(g_name + 2) != 117) {
    return 0;
  }
  if (loadb(g_name + 3) != 115) {
    return 0;
  }
  if (loadb(g_name + 4) != 101) {
    return 0;
  }
  if (loadb(g_name + 5) != 95) {
    return 0;
  }
  if (loadb(g_name + 6) != 121) {
    return 0;
  }
  return 1;
}

int is_mb() {
  if (g_nlen != 9) {
    return 0;
  }
  if (loadb(g_name) != 109) {
    return 0;
  }
  if (loadb(g_name + 1) != 111) {
    return 0;
  }
  if (loadb(g_name + 2) != 117) {
    return 0;
  }
  if (loadb(g_name + 3) != 115) {
    return 0;
  }
  if (loadb(g_name + 4) != 101) {
    return 0;
  }
  if (loadb(g_name + 5) != 95) {
    return 0;
  }
  if (loadb(g_name + 6) != 98) {
    return 0;
  }
  if (loadb(g_name + 7) != 116) {
    return 0;
  }
  if (loadb(g_name + 8) != 110) {
    return 0;
  }
  return 1;
}

int is_fill() {
  if (g_nlen != 7) {
    return 0;
  }
  if (loadb(g_name) != 102) {
    return 0;
  }
  if (loadb(g_name + 1) != 105) {
    return 0;
  }
  if (loadb(g_name + 2) != 108) {
    return 0;
  }
  if (loadb(g_name + 3) != 108) {
    return 0;
  }
  if (loadb(g_name + 4) != 114) {
    return 0;
  }
  if (loadb(g_name + 5) != 103) {
    return 0;
  }
  if (loadb(g_name + 6) != 98) {
    return 0;
  }
  return 1;
}

int is_text() {
  if (g_nlen != 4) {
    return 0;
  }
  if (loadb(g_name) != 116) {
    return 0;
  }
  if (loadb(g_name + 1) != 101) {
    return 0;
  }
  if (loadb(g_name + 2) != 120) {
    return 0;
  }
  if (loadb(g_name + 3) != 116) {
    return 0;
  }
  return 1;
}

int is_spawn() {
  if (g_nlen != 9) {
    return 0;
  }
  if (loadb(g_name) != 97) {
    return 0;
  }
  if (loadb(g_name + 1) != 112) {
    return 0;
  }
  if (loadb(g_name + 2) != 112) {
    return 0;
  }
  if (loadb(g_name + 3) != 95) {
    return 0;
  }
  if (loadb(g_name + 4) != 115) {
    return 0;
  }
  if (loadb(g_name + 5) != 112) {
    return 0;
  }
  if (loadb(g_name + 6) != 97) {
    return 0;
  }
  if (loadb(g_name + 7) != 119) {
    return 0;
  }
  if (loadb(g_name + 8) != 110) {
    return 0;
  }
  return 1;
}

int is_arg() {
  if (g_nlen != 7) {
    return 0;
  }
  if (loadb(g_name) != 97) {
    return 0;
  }
  if (loadb(g_name + 1) != 112) {
    return 0;
  }
  if (loadb(g_name + 2) != 112) {
    return 0;
  }
  if (loadb(g_name + 3) != 95) {
    return 0;
  }
  if (loadb(g_name + 4) != 97) {
    return 0;
  }
  if (loadb(g_name + 5) != 114) {
    return 0;
  }
  if (loadb(g_name + 6) != 103) {
    return 0;
  }
  return 1;
}

int is_wait() {
  if (g_nlen != 4) {
    return 0;
  }
  if (loadb(g_name) != 119) {
    return 0;
  }
  if (loadb(g_name + 1) != 97) {
    return 0;
  }
  if (loadb(g_name + 2) != 105) {
    return 0;
  }
  if (loadb(g_name + 3) != 116) {
    return 0;
  }
  return 1;
}

int is_ticks() {
  if (g_nlen != 5) {
    return 0;
  }
  if (loadb(g_name) != 116) {
    return 0;
  }
  if (loadb(g_name + 1) != 105) {
    return 0;
  }
  if (loadb(g_name + 2) != 99) {
    return 0;
  }
  if (loadb(g_name + 3) != 107) {
    return 0;
  }
  if (loadb(g_name + 4) != 115) {
    return 0;
  }
  return 1;
}

int bi_desk() {
  if (g_bi != 0) {
    return 0;
  }
  if (is_dispw() == 1) {
    g_bi = 103;
  }
  if (is_disph() == 1) {
    g_bi = 104;
  }
  if (is_view() == 1) {
    g_bi = 39;
  }
  if (is_surf() == 1) {
    g_bi = 88;
  }
  if (is_mx() == 1) {
    g_bi = 80;
  }
  if (is_my() == 1) {
    g_bi = 81;
  }
  if (is_mb() == 1) {
    g_bi = 82;
  }
  if (is_fill() == 1) {
    g_bi = 85;
  }
  if (is_text() == 1) {
    g_bi = 86;
  }
  if (is_spawn() == 1) {
    g_bi = 96;
  }
  if (is_arg() == 1) {
    g_bi = 107;
  }
  if (is_wait() == 1) {
    g_bi = 12;
  }
  if (is_ticks() == 1) {
    g_bi = 11;
  }
  return 0;
}

int cmp() {
  g_k = 0;
  while (g_k < 16) {
    g_b = loadb(g_name + g_k);
    g_c2 = loadb(g_base + g_k);
    if (g_b != g_c2) {
      return 0;
    }
    g_k = g_k + 1;
  }
  return 1;
}

int findg() {
  g_j = 0;
  while (g_j < g_ng) {
    g_base = g_names + g_j * 16;
    if (cmp() == 1) {
      g_ret = g_j;
      return g_ret;
    }
    g_j = g_j + 1;
  }
  g_ret = 0 - 1;
  return g_ret;
}

int addg() {
  g_base = g_names + g_ng * 16;
  g_k = 0;
  while (g_k < 16) {
    g_b = loadb(g_name + g_k);
    storeb(g_base + g_k, g_b);
    g_k = g_k + 1;
  }
  g_ng = g_ng + 1;
  return 0;
}

int findf() {
  g_j = 0;
  while (g_j < g_nf) {
    g_base = g_fnames + g_j * 16;
    if (cmp() == 1) {
      g_at = g_fpc + g_j * 4;
      ld32();
      return g_ret;
    }
    g_j = g_j + 1;
  }
  g_ret = 0 - 1;
  return g_ret;
}

int addf() {
  g_base = g_fnames + g_nf * 16;
  g_k = 0;
  while (g_k < 16) {
    g_b = loadb(g_name + g_k);
    storeb(g_base + g_k, g_b);
    g_k = g_k + 1;
  }
  g_at = g_fpc + g_nf * 4;
  g_num = g_pc;
  st32();
  g_nf = g_nf + 1;
  return 0;
}

int savename() {
  g_base = g_cfx + g_nc * 20;
  g_k = 0;
  while (g_k < 16) {
    g_b = loadb(g_name + g_k);
    storeb(g_base + g_k, g_b);
    g_k = g_k + 1;
  }
  g_pending = g_nc;
  g_nc = g_nc + 1;
  return 0;
}

int setpend() {
  g_at = g_cfx + g_pending * 20;
  g_at = g_at + 16;
  g_num = g_pc;
  st32();
  return 0;
}

int patchcalls() {
  g_hi = 0;
  while (g_hi < g_nc) {
    g_base = g_cfx + g_hi * 20;
    g_k = 0;
    while (g_k < 16) {
      g_b = loadb(g_base + g_k);
      storeb(g_name + g_k, g_b);
      g_k = g_k + 1;
    }
    g_at = g_base + 16;
    ld32();
    g_site = g_ret;
    findf();
    g_fnpc = g_ret;
    g_rel = g_fnpc - g_site;
    g_rel = g_rel - 4;
    g_num = g_rel;
    g_at = g_out + 16 + g_site;
    st32();
    g_hi = g_hi + 1;
  }
  return 0;
}

int addloc() {
  g_base = g_lnames + g_nloc * 16;
  g_k = 0;
  while (g_k < 16) {
    g_b = loadb(g_name + g_k);
    storeb(g_base + g_k, g_b);
    g_k = g_k + 1;
  }
  g_at = g_laddr + g_nloc * 4;
  g_num = g_locbase;
  st32();
  storeb(g_lwid + g_nloc, g_scale);
  storeb(g_larr + g_nloc, g_arr);
  g_nloc = g_nloc + 1;
  g_locbase = g_locbase + g_sz;
  g_k = g_locbase & 3;
  if (g_k != 0) {
    g_b = 4 - g_k;
    g_locbase = g_locbase + g_b;
  }
  return 0;
}

int findloc() {
  g_j = 0;
  while (g_j < g_nloc) {
    g_base = g_lnames + g_j * 16;
    if (cmp() == 1) {
      g_at = g_laddr + g_j * 4;
      ld32();
      g_addr = g_ret;
      g_scale = loadb(g_lwid + g_j);
      g_arr = loadb(g_larr + g_j);
      g_ret = g_j;
      return g_ret;
    }
    g_j = g_j + 1;
  }
  g_ret = 0 - 1;
  return g_ret;
}

int rdstr() {
  g_i = g_i + 1;
  g_saddr = 32768 + g_poff;
  while (1) {
    if (g_i >= g_len) {
      return 0;
    }
    g_c = loadb(g_src + g_i);
    if (g_c == 34) {
      g_i = g_i + 1;
      storeb(g_sbuf + g_poff, 0);
      g_poff = g_poff + 1;
      g_num = g_saddr;
      epush();
      return 1;
    }
    if (g_c == 92) {
      g_i = g_i + 1;
      if (g_i < g_len) {
        g_c = loadb(g_src + g_i);
      }
    }
    storeb(g_sbuf + g_poff, g_c);
    g_poff = g_poff + 1;
    g_i = g_i + 1;
  }
  return 0;
}

int load_index() {
  g_i = g_i + 1;
  g_push = g_addr;
  stfix();
  g_push = g_scale;
  stfix();
  g_push = g_bytes;
  stfix();
  expr();
  popfix();
  g_bytes = g_pop;
  popfix();
  g_scale = g_pop;
  popfix();
  g_addr = g_pop;
  if (g_scale != 1) {
    g_num = g_scale;
    epush();
    g_op = 4;
    op();
  }
  g_num = g_addr;
  epush();
  g_op = 2;
  op();
  sws();
  if (g_c == 93) {
    g_i = g_i + 1;
  }
  if (g_bytes == 1) {
    g_op = 13;
    if (g_scale == 1) {
      g_op = 26;
    }
    op();
  }
  return 0;
}

int decl_local() {
  g_scale = 4;
  g_arr = 0;
  g_sz = 4;
  if (g_bytes == 1) {
    g_scale = 1;
    g_sz = 1;
  }
  rdname();
  sws();
  if (g_c == 91) {
    g_arr = 1;
    g_i = g_i + 1;
    rdnum();
    g_sz = g_num;
    if (g_bytes == 0) {
      g_sz = g_num * 4;
    }
    sws();
    if (g_c == 93) {
      g_i = g_i + 1;
    }
  }
  addloc();
  sws();
  if (g_c == 59) {
    g_i = g_i + 1;
  }
  return 0;
}

int prim() {
  sws();
  if (g_c == 40) {
    g_i = g_i + 1;
    expr();
    sws();
    if (g_c == 41) {
      g_i = g_i + 1;
    }
    return 0;
  }
  if (g_c == 34) {
    rdstr();
    return 0;
  }
  if (g_c >= 48) {
    if (g_c <= 57) {
      rdnum();
      epush();
      return 0;
    }
  }
  rdname();
  sws();
  if (g_c == 40) {
    call();
    return 0;
  }
  findloc();
  if (g_ret < 0) {
    findg();
    g_slot = g_ret;
    g_addr = g_slot * 4;
    g_addr = g_addr + 256;
    g_scale = 4;
    g_arr = 0;
  }
  if (g_c == 91) {
    g_bytes = 1;
    load_index();
    return 0;
  }
  if (g_arr == 1) {
    g_num = g_addr;
    epush();
    return 0;
  }
  g_num = g_addr;
  epush();
  g_op = 13;
  op();
  return 0;
}

int call() {
  g_i = g_i + 1;
  g_bi = 0;
  if (is_loadb() == 1) {
    g_bi = 1;
  }
  if (is_storeb() == 1) {
    g_bi = 2;
  }
  if (is_fopen() == 1) {
    g_bi = 50;
  }
  if (is_fclose() == 1) {
    g_bi = 51;
  }
  if (is_fread() == 1) {
    g_bi = 52;
  }
  if (is_fwrite() == 1) {
    g_bi = 53;
  }
  if (is_malloc() == 1) {
    g_bi = 56;
  }
  bi_desk();
  g_fnpc = 0 - 1;
  g_pending = 0 - 1;
  if (g_bi == 0) {
    findf();
    g_fnpc = g_ret;
    if (g_fnpc < 0) {
      savename();
    }
  }
  g_push = g_bi;
  stfix();
  g_push = g_fnpc;
  stfix();
  g_push = g_pending;
  stfix();
  g_argc = 0;
  g_stop = 0;
  sws();
  if (g_c == 41) {
    g_stop = 1;
  }
  while (g_stop == 0) {
    g_push = 0;
    stfix();
    expr();
    popfix();
    g_stop = g_pop;
    g_argc = g_argc + 1;
    sws();
    if (g_c == 44) {
      g_i = g_i + 1;
    }
    if (g_c != 44) {
      g_stop = 1;
    }
  }
  sws();
  if (g_c == 41) {
    g_i = g_i + 1;
  }
  popfix();
  g_pending = g_pop;
  popfix();
  g_fnpc = g_pop;
  popfix();
  g_bi = g_pop;
  g_leave = 1;
  if (g_bi == 1) {
    g_op = 26;
    op();
  }
  if (g_bi == 2) {
    g_op = 16;
    op();
    g_op = 27;
    op();
    g_leave = 0;
  }
  if (g_bi > 2) {
    g_num = g_bi;
    epush();
    g_op = 32;
    op();
  }
  if (g_bi == 12) {
    g_leave = 0;
  }
  if (g_bi == 39) {
    g_leave = 0;
  }
  if (g_bi == 85) {
    g_leave = 0;
  }
  if (g_bi == 86) {
    g_leave = 0;
  }
  if (g_bi == 88) {
    g_leave = 0;
  }
  if (g_bi == 0) {
    g_op = 36;
    op();
    if (g_fnpc < 0) {
      setpend();
      g_num = 0;
      eu();
    }
    if (g_fnpc >= 0) {
      g_rel = g_fnpc - g_pc;
      g_rel = g_rel - 4;
      g_num = g_rel;
      eu();
    }
  }
  return 0;
}

int mul() {
  prim();
  while (1) {
    sws();
    g_more = 0;
    if (g_c == 42) {
      g_more = 1;
    }
    if (g_c == 47) {
      g_more = 1;
    }
    if (g_c == 62) {
      if (loadb(g_src + g_i + 1) == 62) {
        g_more = 1;
      }
    }
    if (g_c == 37) {
      g_more = 1;
    }
    if (g_more == 0) {
      return 0;
    }
    if (g_c == 42) {
      g_i = g_i + 1;
      prim();
      g_op = 4;
      op();
    }
    if (g_c == 47) {
      g_i = g_i + 1;
      prim();
      g_op = 5;
      op();
    }
    if (g_c == 62) {
      if (loadb(g_src + g_i + 1) == 62) {
        g_i = g_i + 2;
        prim();
        g_op = 58;
        op();
      }
    }
    if (g_c == 37) {
      g_i = g_i + 1;
      prim();
      g_op = 20;
      op();
    }
  }
  return 0;
}

int add() {
  mul();
  while (1) {
    sws();
    g_more = 0;
    if (g_c == 43) {
      g_more = 1;
    }
    if (g_c == 45) {
      g_more = 1;
    }
    if (g_c == 38) {
      g_more = 1;
    }
    if (g_c == 94) {
      g_more = 1;
    }
    if (g_more == 0) {
      return 0;
    }
    if (g_c == 43) {
      g_i = g_i + 1;
      mul();
      g_op = 2;
      op();
    }
    if (g_c == 45) {
      g_i = g_i + 1;
      mul();
      g_op = 3;
      op();
    }
    if (g_c == 38) {
      g_i = g_i + 1;
      mul();
      g_op = 53;
      op();
    }
    if (g_c == 94) {
      g_i = g_i + 1;
      mul();
      g_op = 55;
      op();
    }
  }
  return 0;
}

int expr() {
  add();
  sws();
  if (g_c == 61) {
    if (loadb(g_src + g_i + 1) == 61) {
      g_i = g_i + 2;
      add();
      g_op = 17;
      op();
    }
  }
  sws();
  if (g_c == 33) {
    if (loadb(g_src + g_i + 1) == 61) {
      g_i = g_i + 2;
      add();
      g_op = 21;
      op();
    }
  }
  sws();
  if (g_c == 60) {
    g_kind = 18;
    g_i = g_i + 1;
    if (loadb(g_src + g_i) == 61) {
      g_kind = 22;
      g_i = g_i + 1;
    }
    g_push = g_kind;
    stfix();
    add();
    popfix();
    g_op = g_pop;
    op();
  }
  sws();
  if (g_c == 62) {
    if (loadb(g_src + g_i + 1) != 62) {
      g_kind = 23;
      g_i = g_i + 1;
      if (loadb(g_src + g_i) == 61) {
        g_kind = 24;
        g_i = g_i + 1;
      }
      g_push = g_kind;
      stfix();
      add();
      popfix();
      g_op = g_pop;
      op();
    }
  }
  return 0;
}

int do_if() {
  expr();
  sws();
  if (g_c == 41) {
    g_i = g_i + 1;
  }
  g_site = g_pc;
  g_op = 34;
  op();
  g_num = 0;
  eu();
  g_push = g_site;
  stfix();
  blk();
  popfix();
  g_site = g_pop;
  sws();
  g_push = g_i;
  stfix();
  rdname();
  if (is_else() == 1) {
    popfix();
    g_op = 33;
    op();
    g_num = 0;
    eu();
    g_push = g_pc - 4;
    stfix();
    g_rel = g_pc - g_site;
    g_rel = g_rel - 5;
    g_num = g_rel;
    g_at = g_out + 16 + g_site;
    g_at = g_at + 1;
    st32();
    blk();
    popfix();
    g_addr = g_pop;
    g_rel = g_pc - g_addr;
    g_rel = g_rel - 4;
    g_num = g_rel;
    g_at = g_out + 16 + g_addr;
    st32();
    return 0;
  }
  popfix();
  g_i = g_pop;
  g_rel = g_pc - g_site;
  g_rel = g_rel - 5;
  g_num = g_rel;
  g_at = g_out + 16 + g_site;
  g_at = g_at + 1;
  st32();
  return 0;
}

int do_while() {
  g_push = g_pc;
  stfix();
  expr();
  sws();
  if (g_c == 41) {
    g_i = g_i + 1;
  }
  g_site = g_pc;
  g_op = 34;
  op();
  g_num = 0;
  eu();
  g_push = g_site;
  stfix();
  blk();
  popfix();
  g_site = g_pop;
  popfix();
  g_save = g_pop;
  g_op = 33;
  op();
  g_rel = g_save - g_pc;
  g_rel = g_rel - 4;
  g_num = g_rel;
  eu();
  g_rel = g_pc - g_site;
  g_rel = g_rel - 5;
  g_num = g_rel;
  g_at = g_out + 16 + g_site;
  g_at = g_at + 1;
  st32();
  return 0;
}

int stmt() {
  g_mark = g_i;
  sws();
  rdname();
  if (is_int() == 1) {
    g_bytes = 0;
    decl_local();
    return 0;
  }
  if (is_char() == 1) {
    g_bytes = 1;
    decl_local();
    return 0;
  }
  if (is_if() == 1) {
    sws();
    g_i = g_i + 1;
    do_if();
    return 0;
  }
  if (is_while() == 1) {
    sws();
    g_i = g_i + 1;
    do_while();
    return 0;
  }
  if (is_ret() == 1) {
    expr();
    sws();
    if (g_c == 59) {
      g_i = g_i + 1;
    }
    g_op = 12;
    if (g_main == 1) {
      g_op = 8;
    }
    op();
    return 0;
  }
  sws();
  if (g_c == 91) {
    findloc();
    g_slot = g_ret;
    if (g_slot < 0) {
      findg();
      g_addr = g_ret * 4;
      g_addr = g_addr + 256;
      g_scale = 4;
      g_arr = 0;
    }
    g_bytes = 0;
    load_index();
    sws();
    if (g_c == 61) {
      if (loadb(g_src + g_i + 1) != 61) {
        g_i = g_i + 1;
        g_push = g_scale;
        stfix();
        expr();
        popfix();
        g_scale = g_pop;
        g_op = 16;
        op();
        g_op = 14;
        if (g_scale == 1) {
          g_op = 27;
        }
        op();
        sws();
        if (g_c == 59) {
          g_i = g_i + 1;
        }
        return 0;
      }
    }
    return 0;
  }
  if (g_c == 61) {
    if (loadb(g_src + g_i + 1) != 61) {
      findloc();
      g_slot = g_ret;
      if (g_slot < 0) {
        findg();
        g_save = g_ret * 4;
        g_save = g_save + 256;
      }
      if (g_slot >= 0) {
        g_save = g_addr;
      }
      g_i = g_i + 1;
      expr();
      g_num = g_save;
      epush();
      g_op = 14;
      op();
      sws();
      if (g_c == 59) {
        g_i = g_i + 1;
      }
      return 0;
    }
  }
  if (g_c == 40) {
    call();
    sws();
    if (g_c == 59) {
      g_i = g_i + 1;
    }
    if (g_leave == 1) {
      g_op = 15;
      op();
    }
    return 0;
  }
  if (g_i == g_mark) {
    g_i = g_i + 1;
  }
  return 0;
}

int blk() {
  sws();
  if (g_c != 123) {
    return 0;
  }
  g_i = g_i + 1;
  while (1) {
    sws();
    if (g_c == 125) {
      g_i = g_i + 1;
      return 0;
    }
    if (g_c == 0) {
      return 0;
    }
    g_mark = g_i;
    stmt();
    if (g_i == g_mark) {
      g_i = g_i + 1;
    }
  }
  return 0;
}

int emitstrs() {
  if (g_poff == 0) {
    return 0;
  }
  g_strinit = g_pc;
  g_k = 0;
  while (g_k < g_poff) {
    g_num = loadb(g_sbuf + g_k);
    epush();
    g_num = 32768 + g_k;
    epush();
    g_op = 27;
    op();
    g_k = g_k + 1;
  }
  g_op = 33;
  op();
  g_rel = g_mainpc - g_pc;
  g_rel = g_rel - 4;
  g_num = g_rel;
  eu();
  return 1;
}

int toplevel() {
  g_op = 33;
  op();
  g_boot = g_pc;
  g_num = 0;
  eu();
  while (1) {
    sws();
    if (g_c == 0) {
      g_ok = emitstrs();
      patchcalls();
      g_rel = g_mainpc - g_boot;
      g_rel = g_rel - 4;
      if (g_ok == 1) {
        g_rel = g_strinit - g_boot;
        g_rel = g_rel - 4;
      }
      g_num = g_rel;
      g_at = g_out + 16 + g_boot;
      st32();
      g_entry = 0;
      return 0;
    }
    g_mark = g_i;
    rdname();
    g_ty = 0;
    if (is_int() == 1) {
      g_ty = 1;
    }
    if (is_void() == 1) {
      g_ty = 1;
    }
    if (g_ty == 0) {
      if (g_i == g_mark) {
        g_i = g_i + 1;
      }
    }
    if (g_ty == 1) {
      rdname();
      sws();
      g_kind = 0;
      if (g_c == 40) {
        g_kind = 1;
      }
      if (g_kind == 1) {
        g_main = 0;
        if (is_mainn() == 1) {
          g_main = 1;
        }
        addf();
        if (g_main == 1) {
          g_mainpc = g_pc;
        }
        g_nloc = 0;
        g_nparam = 0;
        g_locbase = g_nf - 1;
        g_locbase = g_locbase * 256;
        g_locbase = g_locbase + 8192;
        g_i = g_i + 1;
        sws();
        g_stop = 0;
        while (g_stop == 0) {
          if (g_c == 41) {
            g_stop = 1;
          }
          if (g_c == 0) {
            g_stop = 1;
          }
          if (g_stop == 0) {
            rdname();
            g_bytes = 0;
            if (is_char() == 1) {
              g_bytes = 1;
            }
            g_scale = 4;
            g_arr = 0;
            g_sz = 4;
            if (g_bytes == 1) {
              g_scale = 1;
              g_sz = 1;
            }
            rdname();
            addloc();
            g_nparam = g_nparam + 1;
            sws();
            if (g_c == 44) {
              g_i = g_i + 1;
              sws();
            }
          }
        }
        if (g_c == 41) {
          g_i = g_i + 1;
        }
        g_k = g_nparam - 1;
        while (g_k >= 0) {
          g_at = g_laddr + g_k * 4;
          ld32();
          g_num = g_ret;
          epush();
          g_op = 14;
          op();
          g_k = g_k - 1;
        }
        blk();
        g_op = 12;
        if (g_main == 1) {
          g_op = 8;
        }
        op();
      }
      if (g_kind == 0) {
        addg();
        sws();
        if (g_c == 59) {
          g_i = g_i + 1;
        }
      }
    }
  }
  return 0;
}

int mask32() {
  g_lo = g_h & 65535;
  g_hi = g_h >> 16;
  g_hi = g_hi & 65535;
  g_hi = g_hi * 65536;
  g_h = g_hi + g_lo;
  return 0;
}

int wr() {
  storeb(g_out, 67);
  storeb(g_out + 1, 76);
  storeb(g_out + 2, 86);
  storeb(g_out + 3, 77);
  storeb(g_out + 4, 1);
  storeb(g_out + 5, 1);
  storeb(g_out + 6, g_entry & 255);
  g_b = g_entry >> 8;
  storeb(g_out + 7, g_b & 255);
  storeb(g_out + 8, g_pc & 255);
  g_b = g_pc >> 8;
  storeb(g_out + 9, g_b & 255);
  g_b = g_pc >> 16;
  storeb(g_out + 10, g_b & 255);
  g_b = g_pc >> 24;
  storeb(g_out + 11, g_b & 255);
  g_h = 33052 * 65536;
  g_h = g_h + 40389;
  mask32();
  g_k = 0;
  while (g_k < g_pc) {
    g_b = loadb(g_out + 16 + g_k);
    g_h = g_h ^ g_b;
    mask32();
    g_h = g_h * 16777619;
    mask32();
    g_k = g_k + 1;
  }
  storeb(g_out + 12, g_h & 255);
  g_b = g_h >> 8;
  storeb(g_out + 13, g_b & 255);
  g_b = g_h >> 16;
  storeb(g_out + 14, g_b & 255);
  g_b = g_h >> 24;
  storeb(g_out + 15, g_b & 255);
  if (loadb(g_outpath) == 0) {
    fill_out();
  }
  g_fd = fopen(g_outpath);
  if (g_fd < 0) {
    return 0;
  }
  g_num = 16 + g_pc;
  fwrite(g_fd, g_out, g_num);
  fclose(g_fd);
  return 0;
}

int fill_in() {
  g_k = 0;
  while (g_k < 32) {
    storeb(g_path + g_k, 0);
    g_k = g_k + 1;
  }
  storeb(g_path, 65);
  storeb(g_path + 1, 80);
  storeb(g_path + 2, 80);
  storeb(g_path + 3, 83);
  storeb(g_path + 4, 47);
  storeb(g_path + 5, 67);
  storeb(g_path + 6, 67);
  storeb(g_path + 7, 47);
  storeb(g_path + 8, 73);
  storeb(g_path + 9, 78);
  storeb(g_path + 10, 46);
  storeb(g_path + 11, 67);
  storeb(g_path + 12, 67);
  return 0;
}

int fill_out() {
  g_k = 0;
  while (g_k < 32) {
    storeb(g_outpath + g_k, 0);
    g_k = g_k + 1;
  }
  storeb(g_outpath, 65);
  storeb(g_outpath + 1, 80);
  storeb(g_outpath + 2, 80);
  storeb(g_outpath + 3, 83);
  storeb(g_outpath + 4, 47);
  storeb(g_outpath + 5, 67);
  storeb(g_outpath + 6, 67);
  storeb(g_outpath + 7, 47);
  storeb(g_outpath + 8, 79);
  storeb(g_outpath + 9, 85);
  storeb(g_outpath + 10, 84);
  storeb(g_outpath + 11, 46);
  storeb(g_outpath + 12, 67);
  storeb(g_outpath + 13, 76);
  storeb(g_outpath + 14, 86);
  return 0;
}

int take_arg() {
  g_ok = app_arg(g_arg);
  if (g_ok < 1) {
    return 0;
  }
  g_k = 0;
  g_j = 0;
  g_stop = 0;
  while (g_stop == 0) {
    g_c = loadb(g_arg + g_k);
    if (g_c == 0) {
      g_stop = 1;
    }
    if (g_c == 32) {
      g_stop = 1;
    }
    if (g_c == 44) {
      g_stop = 1;
    }
    if (g_stop == 0) {
      storeb(g_path + g_j, g_c);
      g_j = g_j + 1;
      g_k = g_k + 1;
    }
  }
  storeb(g_path + g_j, 0);
  if (g_c != 32) {
    if (g_c != 44) {
      return 0;
    }
  }
  g_k = g_k + 1;
  g_j = 0;
  g_stop = 0;
  while (g_stop == 0) {
    g_c = loadb(g_arg + g_k);
    if (g_c == 0) {
      g_stop = 1;
    }
    if (g_stop == 0) {
      storeb(g_outpath + g_j, g_c);
      g_j = g_j + 1;
      g_k = g_k + 1;
    }
  }
  storeb(g_outpath + g_j, 0);
  if (g_j < 1) {
    return 0;
  }
  return 1;
}

int main() {
  g_src = malloc(131072);
  g_out = malloc(131072);
  g_name = malloc(32);
  g_names = malloc(4096);
  g_fnames = malloc(4096);
  g_fpc = malloc(1024);
  g_fix = malloc(1024);
  g_cfx = malloc(8192);
  g_path = malloc(128);
  g_outpath = malloc(128);
  g_arg = malloc(256);
  g_lnames = malloc(1024);
  g_laddr = malloc(256);
  g_lwid = malloc(64);
  g_larr = malloc(64);
  g_sbuf = malloc(8192);
  g_nc = 0;
  g_pc = 0;
  g_i = 0;
  g_ng = 0;
  g_nf = 0;
  g_fixsp = 0;
  g_entry = 0;
  g_len = 0;
  g_poff = 0;
  g_nloc = 0;
  g_k = 0;
  while (g_k < 32) {
    storeb(g_outpath + g_k, 0);
    g_k = g_k + 1;
  }
  if (take_arg() == 0) {
    fill_in();
    fill_out();
  }
  g_fd = fopen(g_path);
  g_ok = 0;
  if (g_fd < 0) {
    g_ok = 1;
  }
  if (g_ok == 0) {
    g_len = fread(g_fd, g_src, 120000);
    fclose(g_fd);
  }
  if (g_len < 0) {
    g_len = 0;
  }
  toplevel();
  wr();
  return 0;
}
