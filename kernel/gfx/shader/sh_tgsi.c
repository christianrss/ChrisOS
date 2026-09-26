#include "sh_int.h"

static void op0(char *d, int cap, int *n, int *pc, const char *text) {
    sh_app(d, cap, n, "  ");
    sh_app_i(d, cap, n, *pc);
    sh_app(d, cap, n, ": ");
    sh_app(d, cap, n, text);
    sh_app(d, cap, n, "\n");
    (*pc)++;
}

static void reg(char *d, int cap, int *n, const char *file, int idx) {
    sh_app(d, cap, n, file);
    sh_app_ch(d, cap, n, '[');
    sh_app_i(d, cap, n, idx);
    sh_app_ch(d, cap, n, ']');
}

static void operand(char *d, int cap, int *n, int tmp, int mask, int ncomp, int source) {
    const char *ch = "xyzw";
    int i;
    int count;
    reg(d, cap, n, "TEMP", tmp);
    if (ncomp >= 4 && mask == (0 | (1 << 2) | (2 << 4) | (3 << 6))) {
        return;
    }
    count = source ? 4 : ncomp;
    if (count <= 0) {
        return;
    }
    sh_app_ch(d, cap, n, '.');
    for (i = 0; i < count; ++i) {
        int lane = 0;
        if (i < ncomp) {
            lane = (mask >> (i * 2)) & 3;
        }
        sh_app_ch(d, cap, n, ch[lane & 3]);
    }
}

static void alu2(char *d, int cap, int *n, int *pc, const char *op, int dst, int a, int b,
                 int ncomp) {
    char line[160];
    int ln = 0;
    int mask = 0;
    int i;
    line[0] = 0;
    for (i = 0; i < ncomp && i < 4; ++i) {
        mask |= i << (i * 2);
    }
    sh_app(line, (int)sizeof line, &ln, op);
    sh_app(line, (int)sizeof line, &ln, " ");
    operand(line, (int)sizeof line, &ln, dst, mask, ncomp < 4 ? ncomp : 0, 0);
    sh_app(line, (int)sizeof line, &ln, ", ");
    reg(line, (int)sizeof line, &ln, "TEMP", a);
    sh_app(line, (int)sizeof line, &ln, ", ");
    reg(line, (int)sizeof line, &ln, "TEMP", b);
    op0(d, cap, n, pc, line);
}

static void mad_col(char *d, int cap, int *n, int *pc, int dst, int vec, int mat, int cols) {
    const char *sw[4] = {".xxxx", ".yyyy", ".zzzz", ".wwww"};
    char line[180];
    int ln;
    int k;
    for (k = 0; k < cols; ++k) {
        ln = 0;
        line[0] = 0;
        sh_app(line, (int)sizeof line, &ln, k == 0 ? "MUL " : "MAD ");
        reg(line, (int)sizeof line, &ln, "TEMP", dst);
        sh_app(line, (int)sizeof line, &ln, ", ");
        reg(line, (int)sizeof line, &ln, "TEMP", vec);
        sh_app(line, (int)sizeof line, &ln, sw[k]);
        sh_app(line, (int)sizeof line, &ln, ", ");
        reg(line, (int)sizeof line, &ln, "TEMP", mat + k);
        if (k > 0) {
            sh_app(line, (int)sizeof line, &ln, ", ");
            reg(line, (int)sizeof line, &ln, "TEMP", dst);
        }
        op0(d, cap, n, pc, line);
    }
}

int sh_emit_tgsi(ShComp *c, char *dst, int cap, const int *var_remap) {
    int n = 0;
    int pc = 0;
    int i;
    int saw_fcoord = 0;
    int const_imm[SH_IR_MAX];
    int nimm = 0;
    if (!c || !dst || cap < 64) {
        return -1;
    }
    dst[0] = 0;
    for (i = 0; i < c->nir; ++i) {
        const_imm[i] = -1;
        if (c->ir[i].op == IR_CONST) {
            const_imm[i] = nimm++;
        }
        if (c->ir[i].op == IR_LOAD_FCOORD) {
            saw_fcoord = 1;
        }
    }
    sh_app(dst, cap, &n, c->stage == SH_STAGE_VERTEX ? "VERT\n" : "FRAG\n");
    if (c->stage == SH_STAGE_VERTEX) {
        for (i = 0; i <= c->attr_hi; ++i) {
            sh_app(dst, cap, &n, "DCL IN[");
            sh_app_i(dst, cap, &n, i);
            sh_app(dst, cap, &n, "]\n");
        }
        sh_app(dst, cap, &n, "DCL OUT[0], POSITION\n");
        for (i = 0; i <= c->var_hi; ++i) {
            sh_app(dst, cap, &n, "DCL OUT[");
            sh_app_i(dst, cap, &n, i + 1);
            sh_app(dst, cap, &n, "], GENERIC[");
            sh_app_i(dst, cap, &n, i);
            sh_app(dst, cap, &n, "]\n");
        }
    } else {
        for (i = 0; i <= c->var_hi; ++i) {
            int slot = i;
            if (var_remap && var_remap[i] >= 0) {
                slot = var_remap[i];
            }
            sh_app(dst, cap, &n, "DCL IN[");
            sh_app_i(dst, cap, &n, slot);
            sh_app(dst, cap, &n, "], GENERIC[");
            sh_app_i(dst, cap, &n, slot);
            sh_app(dst, cap, &n, "], PERSPECTIVE\n");
        }
        if (saw_fcoord) {
            sh_app(dst, cap, &n, "DCL IN[7], POSITION\n");
        }
        sh_app(dst, cap, &n, "DCL OUT[0], COLOR\n");
        for (i = 0; i <= c->samp_hi; ++i) {
            sh_app(dst, cap, &n, "DCL SAMP[");
            sh_app_i(dst, cap, &n, i);
            sh_app(dst, cap, &n, "]\nDCL SVIEW[");
            sh_app_i(dst, cap, &n, i);
            sh_app(dst, cap, &n, "], 2D, FLOAT\n");
        }
    }
    if (c->uni_hi >= 0) {
        sh_app(dst, cap, &n, "DCL CONST[0][0..");
        sh_app_i(dst, cap, &n, c->uni_hi);
        sh_app(dst, cap, &n, "]\n");
    }
    if (c->ntmp > 0) {
        sh_app(dst, cap, &n, "DCL TEMP[0");
        if (c->ntmp > 1) {
            sh_app(dst, cap, &n, "..");
            sh_app_i(dst, cap, &n, c->ntmp - 1);
        }
        sh_app(dst, cap, &n, "]\n");
    }
    for (i = 0; i < c->nir; ++i) {
        const Ir *in = &c->ir[i];
        float cv[4];
        int k;
        if (in->op != IR_CONST) {
            continue;
        }
        for (k = 0; k < 4; ++k) {
            cv[k] = 0.f;
        }
        for (k = 0; k < (int)in->ncomp && k < 4; ++k) {
            cv[k] = c->imm[in->aux + k];
        }
        sh_app(dst, cap, &n, "IMM[");
        sh_app_i(dst, cap, &n, const_imm[i]);
        sh_app(dst, cap, &n, "] FLT32 {");
        for (k = 0; k < 4; ++k) {
            if (k) {
                sh_app(dst, cap, &n, ", ");
            }
            sh_app_f(dst, cap, &n, cv[k]);
        }
        sh_app(dst, cap, &n, "}\n");
    }
    for (i = 0; i < c->nir; ++i) {
        const Ir *in = &c->ir[i];
        char line[192];
        int ln;
        if (n + 80 >= cap) {
            sh_err(c, -1, "TGSI exceeds the VirGL command budget");
            dst[0] = 0;
            return -1;
        }
        if (in->op == IR_NOP) {
            continue;
        }
        ln = 0;
        line[0] = 0;
        switch (in->op) {
        case IR_CONST:
            sh_app(line, (int)sizeof line, &ln, "MOV ");
            reg(line, (int)sizeof line, &ln, "TEMP", in->dst);
            sh_app(line, (int)sizeof line, &ln, ", ");
            reg(line, (int)sizeof line, &ln, "IMM", const_imm[i]);
            op0(dst, cap, &n, &pc, line);
            break;
        case IR_MOV:
        case IR_SWZ: {
            int smask = in->op == IR_SWZ ? in->aux : 0;
            int dmask = 0;
            int nc = in->ncomp ? in->ncomp : 4;
            int k;
            for (k = 0; k < nc && k < 4; ++k) {
                dmask |= k << (k * 2);
            }
            if (in->op == IR_MOV) {
                smask = dmask;
            }
            sh_app(line, (int)sizeof line, &ln, "MOV ");
            operand(line, (int)sizeof line, &ln, in->dst, dmask, nc, 0);
            sh_app(line, (int)sizeof line, &ln, ", ");
            operand(line, (int)sizeof line, &ln, in->a, smask, nc, 1);
            op0(dst, cap, &n, &pc, line);
            break;
        }
        case IR_SETLANE: {
            const char *lane = "xyzw";
            const char *comp = "xyzw";
            sh_app(line, (int)sizeof line, &ln, "MOV ");
            reg(line, (int)sizeof line, &ln, "TEMP", in->dst);
            sh_app_ch(line, (int)sizeof line, &ln, '.');
            sh_app_ch(line, (int)sizeof line, &ln, lane[in->aux & 3]);
            sh_app(line, (int)sizeof line, &ln, ", ");
            reg(line, (int)sizeof line, &ln, "TEMP", in->a);
            sh_app_ch(line, (int)sizeof line, &ln, '.');
            sh_app_ch(line, (int)sizeof line, &ln, comp[in->aux2 & 3]);
            sh_app_ch(line, (int)sizeof line, &ln, comp[in->aux2 & 3]);
            sh_app_ch(line, (int)sizeof line, &ln, comp[in->aux2 & 3]);
            sh_app_ch(line, (int)sizeof line, &ln, comp[in->aux2 & 3]);
            op0(dst, cap, &n, &pc, line);
            break;
        }
        case IR_ADD:
            alu2(dst, cap, &n, &pc, "ADD", in->dst, in->a, in->b, in->ncomp ? in->ncomp : 4);
            break;
        case IR_SUB:
            alu2(dst, cap, &n, &pc, "SUB", in->dst, in->a, in->b, in->ncomp ? in->ncomp : 4);
            break;
        case IR_MUL:
            alu2(dst, cap, &n, &pc, "MUL", in->dst, in->a, in->b, in->ncomp ? in->ncomp : 4);
            break;
        case IR_MIN:
            alu2(dst, cap, &n, &pc, "MIN", in->dst, in->a, in->b, in->ncomp ? in->ncomp : 4);
            break;
        case IR_MAX:
            alu2(dst, cap, &n, &pc, "MAX", in->dst, in->a, in->b, in->ncomp ? in->ncomp : 4);
            break;
        case IR_ABS:
            sh_app(line, (int)sizeof line, &ln, "ABS ");
            reg(line, (int)sizeof line, &ln, "TEMP", in->dst);
            sh_app(line, (int)sizeof line, &ln, ", ");
            reg(line, (int)sizeof line, &ln, "TEMP", in->a);
            op0(dst, cap, &n, &pc, line);
            break;
        case IR_DOT:
            sh_app(line, (int)sizeof line, &ln, in->aux == 4 ? "DP4 " : "DP3 ");
            reg(line, (int)sizeof line, &ln, "TEMP", in->dst);
            sh_app(line, (int)sizeof line, &ln, ".x, ");
            reg(line, (int)sizeof line, &ln, "TEMP", in->a);
            sh_app(line, (int)sizeof line, &ln, ", ");
            reg(line, (int)sizeof line, &ln, "TEMP", in->b);
            op0(dst, cap, &n, &pc, line);
            break;
        case IR_RSQ:
        case IR_RCP:
        case IR_SIN:
        case IR_COS:
        case IR_TRUNC: {
            const char *nm = in->op == IR_RSQ ? "RSQ" : in->op == IR_RCP ? "RCP" : in->op == IR_SIN
                                                                                        ? "SIN"
                                                                                    : in->op == IR_COS
                                                                                        ? "COS"
                                                                                        : "TRUNC";
            int nc = in->ncomp ? in->ncomp : 1;
            int k;
            const char *lch = "xyzw";
            for (k = 0; k < nc; ++k) {
                ln = 0;
                line[0] = 0;
                sh_app(line, (int)sizeof line, &ln, nm);
                sh_app(line, (int)sizeof line, &ln, " ");
                reg(line, (int)sizeof line, &ln, "TEMP", in->dst);
                sh_app_ch(line, (int)sizeof line, &ln, '.');
                sh_app_ch(line, (int)sizeof line, &ln, lch[k]);
                sh_app(line, (int)sizeof line, &ln, ", ");
                reg(line, (int)sizeof line, &ln, "TEMP", in->a);
                sh_app_ch(line, (int)sizeof line, &ln, '.');
                sh_app_ch(line, (int)sizeof line, &ln, lch[k]);
                sh_app_ch(line, (int)sizeof line, &ln, lch[k]);
                sh_app_ch(line, (int)sizeof line, &ln, lch[k]);
                sh_app_ch(line, (int)sizeof line, &ln, lch[k]);
                op0(dst, cap, &n, &pc, line);
            }
            break;
        }
        case IR_POW:
            sh_app(line, (int)sizeof line, &ln, "POW ");
            reg(line, (int)sizeof line, &ln, "TEMP", in->dst);
            sh_app(line, (int)sizeof line, &ln, ".x, ");
            reg(line, (int)sizeof line, &ln, "TEMP", in->a);
            sh_app(line, (int)sizeof line, &ln, ".xxxx, ");
            reg(line, (int)sizeof line, &ln, "TEMP", in->b);
            sh_app(line, (int)sizeof line, &ln, ".xxxx");
            op0(dst, cap, &n, &pc, line);
            break;
        case IR_CMP: {
            const char *nm = "SLT";
            int swap = 0;
            if (in->aux == CMP_GT) {
                nm = "SLT";
                swap = 1;
            } else if (in->aux == CMP_GE) {
                nm = "SGE";
            } else if (in->aux == CMP_LE) {
                nm = "SGE";
                swap = 1;
            } else if (in->aux == CMP_EQ) {
                nm = "SEQ";
            } else if (in->aux == CMP_NE) {
                nm = "SNE";
            }
            sh_app(line, (int)sizeof line, &ln, nm);
            sh_app(line, (int)sizeof line, &ln, " ");
            reg(line, (int)sizeof line, &ln, "TEMP", in->dst);
            sh_app(line, (int)sizeof line, &ln, ".x, ");
            reg(line, (int)sizeof line, &ln, "TEMP", swap ? in->b : in->a);
            sh_app(line, (int)sizeof line, &ln, ".xxxx, ");
            reg(line, (int)sizeof line, &ln, "TEMP", swap ? in->a : in->b);
            sh_app(line, (int)sizeof line, &ln, ".xxxx");
            op0(dst, cap, &n, &pc, line);
            break;
        }
        case IR_MULMV:
            mad_col(dst, cap, &n, &pc, in->dst, in->b, in->a, in->aux ? in->aux : 4);
            break;
        case IR_MULMM: {
            int cols = in->aux ? in->aux : 4;
            int col;
            for (col = 0; col < cols; ++col) {
                mad_col(dst, cap, &n, &pc, in->dst + col, in->b + col, in->a, cols);
            }
            break;
        }
        case IR_SAMPLE:
            sh_app(line, (int)sizeof line, &ln, "TEX ");
            reg(line, (int)sizeof line, &ln, "TEMP", in->dst);
            sh_app(line, (int)sizeof line, &ln, ", ");
            reg(line, (int)sizeof line, &ln, "TEMP", in->a);
            sh_app(line, (int)sizeof line, &ln, ", SAMP[");
            sh_app_i(line, (int)sizeof line, &ln, in->aux);
            sh_app(line, (int)sizeof line, &ln, "], 2D");
            op0(dst, cap, &n, &pc, line);
            break;
        case IR_LOAD_ATTR:
            sh_app(line, (int)sizeof line, &ln, "MOV ");
            reg(line, (int)sizeof line, &ln, "TEMP", in->dst);
            sh_app(line, (int)sizeof line, &ln, ", ");
            reg(line, (int)sizeof line, &ln, "IN", in->aux);
            op0(dst, cap, &n, &pc, line);
            break;
        case IR_LOAD_VAR: {
            int slot = in->aux;
            if (var_remap && slot >= 0 && slot < 8 && var_remap[slot] >= 0) {
                slot = var_remap[slot];
            }
            sh_app(line, (int)sizeof line, &ln, "MOV ");
            reg(line, (int)sizeof line, &ln, "TEMP", in->dst);
            sh_app(line, (int)sizeof line, &ln, ", ");
            reg(line, (int)sizeof line, &ln, "IN", slot);
            op0(dst, cap, &n, &pc, line);
            break;
        }
        case IR_LOAD_UNI: {
            int k;
            int nvec = in->aux2 ? in->aux2 : 1;
            for (k = 0; k < nvec; ++k) {
                ln = 0;
                line[0] = 0;
                sh_app(line, (int)sizeof line, &ln, "MOV ");
                reg(line, (int)sizeof line, &ln, "TEMP", in->dst + k);
                sh_app(line, (int)sizeof line, &ln, ", CONST[0][");
                sh_app_i(line, (int)sizeof line, &ln, in->aux + k);
                sh_app_ch(line, (int)sizeof line, &ln, ']');
                op0(dst, cap, &n, &pc, line);
            }
            break;
        }
        case IR_LOAD_FCOORD:
            sh_app(line, (int)sizeof line, &ln, "MOV ");
            reg(line, (int)sizeof line, &ln, "TEMP", in->dst);
            sh_app(line, (int)sizeof line, &ln, ", IN[7]");
            op0(dst, cap, &n, &pc, line);
            break;
        case IR_STORE_POS:
            sh_app(line, (int)sizeof line, &ln, "MOV OUT[0], ");
            reg(line, (int)sizeof line, &ln, "TEMP", in->a);
            op0(dst, cap, &n, &pc, line);
            break;
        case IR_STORE_VAR:
            sh_app(line, (int)sizeof line, &ln, "MOV ");
            reg(line, (int)sizeof line, &ln, "OUT", in->aux + 1);
            sh_app(line, (int)sizeof line, &ln, ", ");
            reg(line, (int)sizeof line, &ln, "TEMP", in->a);
            op0(dst, cap, &n, &pc, line);
            break;
        case IR_STORE_COLOR:
            sh_app(line, (int)sizeof line, &ln, "MOV OUT[0], ");
            reg(line, (int)sizeof line, &ln, "TEMP", in->a);
            op0(dst, cap, &n, &pc, line);
            break;
        case IR_IF:
            sh_app(line, (int)sizeof line, &ln, "IF ");
            reg(line, (int)sizeof line, &ln, "TEMP", in->a);
            sh_app(line, (int)sizeof line, &ln, ".x");
            op0(dst, cap, &n, &pc, line);
            break;
        case IR_ELSE:
            op0(dst, cap, &n, &pc, "ELSE");
            break;
        case IR_ENDIF:
            op0(dst, cap, &n, &pc, "ENDIF");
            break;
        case IR_DISCARD:
            op0(dst, cap, &n, &pc, "KILL");
            break;
        default:
            break;
        }
    }
    sh_app(dst, cap, &n, "  ");
    sh_app_i(dst, cap, &n, pc);
    sh_app(dst, cap, &n, ": END\n");
    return c->nerr ? -1 : 0;
}
