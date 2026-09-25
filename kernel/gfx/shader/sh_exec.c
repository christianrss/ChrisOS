#include "sh_int.h"

static float wrap_pi(float x) {
    const float pi = 3.14159265f;
    const float tw = 6.2831853f;
    int guard = 0;
    while (x > pi && guard++ < 16) {
        x -= tw;
    }
    guard = 0;
    while (x < -pi && guard++ < 16) {
        x += tw;
    }
    return x;
}

static float sh_sinf(float x) {
    float term;
    float t;
    float x2;
    int i;
    int sign = -1;
    x = wrap_pi(x);
    term = x;
    t = x;
    x2 = x * x;
    for (i = 1; i <= 6; ++i) {
        term *= x2;
        term /= (float)((2 * i) * (2 * i + 1));
        if (sign < 0) {
            t -= term;
        } else {
            t += term;
        }
        sign = -sign;
    }
    return t;
}

static float sh_cosf(float x) {
    return sh_sinf(x + 1.5707963f);
}

static float sh_expf(float x) {
    float t = 1.f;
    float term = 1.f;
    int i;
    if (x > 10.f) {
        x = 10.f;
    }
    if (x < -10.f) {
        x = -10.f;
    }
    for (i = 1; i <= 16; ++i) {
        term *= x / (float)i;
        t += term;
    }
    return t;
}

static float sh_logf(float a) {
    int e = 0;
    float u;
    float t = 0.f;
    float term;
    int i;
    int guard = 0;
    if (a <= 0.f) {
        return 0.f;
    }
    while (a > 2.f && guard++ < 32) {
        a *= 0.5f;
        e++;
    }
    guard = 0;
    while (a < 1.f && guard++ < 32) {
        a *= 2.f;
        e--;
    }
    u = a - 1.f;
    term = u;
    for (i = 1; i <= 20; ++i) {
        if (i & 1) {
            t += term / (float)i;
        } else {
            t -= term / (float)i;
        }
        term *= u;
    }
    return t + (float)e * 0.693147f;
}

static float sh_powf(float a, float b) {
    int ib;
    float r;
    int i;
    if (a == 0.f) {
        return 0.f;
    }
    ib = (int)b;
    if ((float)ib == b && ib >= 0 && ib < 16) {
        r = 1.f;
        if (a < 0.f && (ib & 1)) {
            r = -1.f;
            a = -a;
        } else if (a < 0.f) {
            a = -a;
        }
        for (i = 0; i < ib; ++i) {
            r *= a;
        }
        return r;
    }
    if (a < 0.f) {
        return 0.f;
    }
    return sh_expf(b * sh_logf(a));
}

static void sample2d(const uint8_t *tex, int w, int h, float u, float v, float out[4]) {
    int x;
    int y;
    int o;
    out[0] = out[1] = out[2] = out[3] = 0.f;
    if (!tex || w <= 0 || h <= 0) {
        return;
    }
    if (u < 0.f) {
        u = 0.f;
    }
    if (v < 0.f) {
        v = 0.f;
    }
    if (u > 1.f) {
        u = 1.f;
    }
    if (v > 1.f) {
        v = 1.f;
    }
    x = (int)(u * (float)(w - 1) + 0.5f);
    y = (int)(v * (float)(h - 1) + 0.5f);
    if (x < 0) {
        x = 0;
    }
    if (y < 0) {
        y = 0;
    }
    if (x >= w) {
        x = w - 1;
    }
    if (y >= h) {
        y = h - 1;
    }
    o = (y * w + x) * 4;
    out[2] = (float)tex[o] / 255.f;
    out[1] = (float)tex[o + 1] / 255.f;
    out[0] = (float)tex[o + 2] / 255.f;
    out[3] = (float)tex[o + 3] / 255.f;
}

static int skipping(const int *sk, int sp) {
    return sp > 0 && sk[sp - 1];
}

int sh_exec_ir(const ShComp *c, const float *attr, const float *uni, const float *var_in,
               const float fragcoord[4], const uint8_t *tex, int tex_w, int tex_h, float *pos,
               float vary_out[][4], float color[4], int *discarded, const int *var_remap) {
    float tmp[SH_TEMP_MAX][4];
    int sk[32];
    int st[32];
    int sp = 0;
    int i;
    int k;
    if (!c) {
        return -1;
    }
    memset(tmp, 0, sizeof tmp);
    if (discarded) {
        *discarded = 0;
    }
    if (color) {
        color[0] = color[1] = color[2] = color[3] = 0.f;
    }
    if (pos) {
        pos[0] = pos[1] = pos[2] = 0.f;
        pos[3] = 1.f;
    }
    for (i = 0; i < c->nir; ++i) {
        const Ir *in = &c->ir[i];
        int skip = skipping(sk, sp);
        if (in->op == IR_IF) {
            int parent = skip;
            float cond = 0.f;
            if (in->a >= 0 && in->a < c->ntmp) {
                cond = tmp[in->a][0];
            }
            if (sp >= 32) {
                return -1;
            }
            if (parent) {
                sk[sp] = 1;
                st[sp] = 2;
            } else if (cond != 0.f) {
                sk[sp] = 0;
                st[sp] = 0;
            } else {
                sk[sp] = 1;
                st[sp] = 0;
            }
            sp++;
            continue;
        }
        if (in->op == IR_ELSE) {
            if (sp <= 0) {
                return -1;
            }
            if (st[sp - 1] == 2) {
                continue;
            }
            if (sk[sp - 1] == 0) {
                sk[sp - 1] = 1;
            } else {
                sk[sp - 1] = 0;
            }
            st[sp - 1] = 1;
            continue;
        }
        if (in->op == IR_ENDIF) {
            if (sp <= 0) {
                return -1;
            }
            sp--;
            continue;
        }
        if (skip || in->op == IR_NOP) {
            continue;
        }
        if (in->op == IR_DISCARD) {
            if (discarded) {
                *discarded = 1;
            }
            return 0;
        }
        if (in->op == IR_CONST && in->dst >= 0 && in->dst < SH_TEMP_MAX) {
            for (k = 0; k < 4; ++k) {
                tmp[in->dst][k] = 0.f;
            }
            for (k = 0; k < (int)in->ncomp && k < 4; ++k) {
                tmp[in->dst][k] = c->imm[in->aux + k];
            }
        } else if (in->op == IR_MOV && in->dst >= 0 && in->a >= 0) {
            int nc = in->ncomp ? in->ncomp : 4;
            for (k = 0; k < nc && k < 4; ++k) {
                tmp[in->dst][k] = tmp[in->a][k];
            }
        } else if (in->op == IR_SWZ && in->dst >= 0 && in->a >= 0) {
            int nc = in->ncomp ? in->ncomp : 1;
            float s[4];
            for (k = 0; k < 4; ++k) {
                s[k] = tmp[in->a][k];
            }
            for (k = 0; k < 4; ++k) {
                tmp[in->dst][k] = 0.f;
            }
            for (k = 0; k < nc && k < 4; ++k) {
                tmp[in->dst][k] = s[(in->aux >> (k * 2)) & 3];
            }
        } else if (in->op == IR_SETLANE && in->dst >= 0 && in->a >= 0) {
            tmp[in->dst][in->aux & 3] = tmp[in->a][in->aux2 & 3];
        } else if ((in->op == IR_ADD || in->op == IR_SUB || in->op == IR_MUL || in->op == IR_MIN ||
                    in->op == IR_MAX) &&
                   in->dst >= 0 && in->a >= 0 && in->b >= 0) {
            int nc = in->ncomp ? in->ncomp : 4;
            for (k = 0; k < nc && k < 4; ++k) {
                float a = tmp[in->a][k];
                float b = tmp[in->b][k];
                float r = 0.f;
                if (in->op == IR_ADD) {
                    r = a + b;
                } else if (in->op == IR_SUB) {
                    r = a - b;
                } else if (in->op == IR_MUL) {
                    r = a * b;
                } else if (in->op == IR_MIN) {
                    r = a < b ? a : b;
                } else {
                    r = a > b ? a : b;
                }
                tmp[in->dst][k] = r;
            }
        } else if (in->op == IR_ABS && in->dst >= 0 && in->a >= 0) {
            int nc = in->ncomp ? in->ncomp : 4;
            for (k = 0; k < nc && k < 4; ++k) {
                float a = tmp[in->a][k];
                tmp[in->dst][k] = a < 0.f ? -a : a;
            }
        } else if (in->op == IR_DOT && in->dst >= 0) {
            int nc = in->aux ? in->aux : 3;
            float r = 0.f;
            for (k = 0; k < nc && k < 4; ++k) {
                r += tmp[in->a][k] * tmp[in->b][k];
            }
            tmp[in->dst][0] = r;
        } else if (in->op == IR_RSQ && in->dst >= 0 && in->a >= 0) {
            float a = tmp[in->a][0];
            tmp[in->dst][0] = a > 0.f ? 1.f / sh_powf(a, 0.5f) : 0.f;
        } else if (in->op == IR_RCP && in->dst >= 0 && in->a >= 0) {
            int nc = in->ncomp ? in->ncomp : 1;
            for (k = 0; k < nc && k < 4; ++k) {
                float a = tmp[in->a][k];
                tmp[in->dst][k] = a != 0.f ? 1.f / a : 0.f;
            }
        } else if ((in->op == IR_SIN || in->op == IR_COS) && in->dst >= 0 && in->a >= 0) {
            int nc = in->ncomp ? in->ncomp : 1;
            for (k = 0; k < nc && k < 4; ++k) {
                tmp[in->dst][k] = in->op == IR_SIN ? sh_sinf(tmp[in->a][k]) : sh_cosf(tmp[in->a][k]);
            }
        } else if (in->op == IR_POW && in->dst >= 0) {
            tmp[in->dst][0] = sh_powf(tmp[in->a][0], tmp[in->b][0]);
        } else if (in->op == IR_TRUNC && in->dst >= 0 && in->a >= 0) {
            tmp[in->dst][0] = (float)(int)tmp[in->a][0];
        } else if (in->op == IR_CMP && in->dst >= 0) {
            float a = tmp[in->a][0];
            float b = tmp[in->b][0];
            int r = 0;
            if (in->aux == CMP_LT) {
                r = a < b;
            } else if (in->aux == CMP_GT) {
                r = a > b;
            } else if (in->aux == CMP_LE) {
                r = a <= b;
            } else if (in->aux == CMP_GE) {
                r = a >= b;
            } else if (in->aux == CMP_EQ) {
                r = a == b;
            } else {
                r = a != b;
            }
            tmp[in->dst][0] = r ? 1.f : 0.f;
        } else if (in->op == IR_MULMV && in->dst >= 0) {
            int cols = in->aux ? in->aux : 4;
            float r[4];
            r[0] = r[1] = r[2] = r[3] = 0.f;
            for (k = 0; k < cols; ++k) {
                float s = tmp[in->b][k];
                int comp;
                for (comp = 0; comp < cols && comp < 4; ++comp) {
                    r[comp] += tmp[in->a + k][comp] * s;
                }
            }
            for (k = 0; k < 4; ++k) {
                tmp[in->dst][k] = r[k];
            }
        } else if (in->op == IR_MULMM && in->dst >= 0) {
            int cols = in->aux ? in->aux : 4;
            float colv[4][4];
            int col;
            for (col = 0; col < cols; ++col) {
                colv[col][0] = colv[col][1] = colv[col][2] = colv[col][3] = 0.f;
                for (k = 0; k < cols; ++k) {
                    float s = tmp[in->b + col][k];
                    int comp;
                    for (comp = 0; comp < cols && comp < 4; ++comp) {
                        colv[col][comp] += tmp[in->a + k][comp] * s;
                    }
                }
            }
            for (col = 0; col < cols; ++col) {
                for (k = 0; k < 4; ++k) {
                    tmp[in->dst + col][k] = colv[col][k];
                }
            }
        } else if (in->op == IR_SAMPLE && in->dst >= 0 && in->a >= 0) {
            sample2d(tex, tex_w, tex_h, tmp[in->a][0], tmp[in->a][1], tmp[in->dst]);
        } else if (in->op == IR_LOAD_ATTR && in->dst >= 0 && attr) {
            const float *p = attr + in->aux * 4;
            for (k = 0; k < 4; ++k) {
                tmp[in->dst][k] = p[k];
            }
        } else if (in->op == IR_LOAD_VAR && in->dst >= 0 && var_in) {
            int slot = in->aux;
            const float *p;
            if (var_remap && slot >= 0 && slot < 8 && var_remap[slot] >= 0) {
                slot = var_remap[slot];
            }
            p = var_in + slot * 4;
            for (k = 0; k < 4; ++k) {
                tmp[in->dst][k] = p[k];
            }
        } else if (in->op == IR_LOAD_UNI && in->dst >= 0 && uni) {
            int nvec = in->aux2 ? in->aux2 : 1;
            for (k = 0; k < nvec; ++k) {
                int c0;
                for (c0 = 0; c0 < 4; ++c0) {
                    tmp[in->dst + k][c0] = uni[(in->aux + k) * 4 + c0];
                }
            }
        } else if (in->op == IR_LOAD_FCOORD && in->dst >= 0 && fragcoord) {
            for (k = 0; k < 4; ++k) {
                tmp[in->dst][k] = fragcoord[k];
            }
        } else if (in->op == IR_STORE_POS && pos && in->a >= 0) {
            for (k = 0; k < 4; ++k) {
                pos[k] = tmp[in->a][k];
            }
        } else if (in->op == IR_STORE_VAR && vary_out && in->a >= 0 && in->aux >= 0 && in->aux < 8) {
            for (k = 0; k < 4; ++k) {
                vary_out[in->aux][k] = tmp[in->a][k];
            }
        } else if (in->op == IR_STORE_COLOR && color && in->a >= 0) {
            for (k = 0; k < 4; ++k) {
                color[k] = tmp[in->a][k];
            }
        }
    }
    return 0;
}
