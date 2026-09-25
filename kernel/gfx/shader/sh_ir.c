#include "sh_int.h"

static int temp_ok(const ShComp *c, int t) {
    return t >= 0 && t < c->ntmp && t < SH_TEMP_MAX;
}

int sh_verify(ShComp *c) {
    int i;
    int sp = 0;
    int saw_else[32];
    if (!c) {
        return -1;
    }
    if (c->stage != SH_STAGE_VERTEX && c->stage != SH_STAGE_FRAGMENT) {
        sh_err(c, -1, "invalid shader stage");
        return -1;
    }
    if (c->ntmp < 0 || c->ntmp > SH_TEMP_MAX || c->nir < 0 || c->nir > SH_IR_MAX) {
        sh_err(c, -1, "IR size is invalid");
        return -1;
    }
    memset(saw_else, 0, sizeof saw_else);
    for (i = 0; i < c->nir; ++i) {
        const Ir *in = &c->ir[i];
        if (in->op > IR_DISCARD) {
            sh_err(c, -1, "invalid IR opcode");
            return -1;
        }
        if (in->op == IR_NOP) {
            continue;
        }
        if (in->op == IR_IF) {
            if (!temp_ok(c, in->a) || sp >= 32) {
                sh_err(c, -1, "invalid if");
                return -1;
            }
            saw_else[sp++] = 0;
            continue;
        }
        if (in->op == IR_ELSE) {
            if (sp <= 0 || saw_else[sp - 1]) {
                sh_err(c, -1, "invalid else");
                return -1;
            }
            saw_else[sp - 1] = 1;
            continue;
        }
        if (in->op == IR_ENDIF) {
            if (sp <= 0) {
                sh_err(c, -1, "invalid endif");
                return -1;
            }
            sp--;
            continue;
        }
        if (in->op == IR_STORE_POS && c->stage != SH_STAGE_VERTEX) {
            sh_err(c, -1, "position store in a non-vertex shader");
            return -1;
        }
        if (in->op == IR_STORE_COLOR && c->stage != SH_STAGE_FRAGMENT) {
            sh_err(c, -1, "color store in a non-fragment shader");
            return -1;
        }
        if (in->op == IR_DISCARD && c->stage != SH_STAGE_FRAGMENT) {
            sh_err(c, -1, "discard in a non-fragment shader");
            return -1;
        }
        if (in->op == IR_SAMPLE && (in->aux < 0 || in->aux > c->samp_hi)) {
            sh_err(c, -1, "invalid sampler");
            return -1;
        }
        if (in->op == IR_LOAD_ATTR && (in->aux < 0 || in->aux > c->attr_hi)) {
            sh_err(c, -1, "invalid attribute");
            return -1;
        }
        if ((in->op == IR_LOAD_VAR || in->op == IR_STORE_VAR) &&
            (in->aux < 0 || in->aux > 7)) {
            sh_err(c, -1, "invalid varying");
            return -1;
        }
        if (in->op == IR_LOAD_UNI) {
            int n = in->aux2;
            if (n < 1 || in->aux < 0 || !temp_ok(c, in->dst) || !temp_ok(c, in->dst + n - 1)) {
                sh_err(c, -1, "invalid uniform load");
                return -1;
            }
        }
        if (in->op == IR_MULMM) {
            int n = in->aux;
            if ((n != 3 && n != 4) || !temp_ok(c, in->dst) || !temp_ok(c, in->dst + n - 1) ||
                !temp_ok(c, in->a) || !temp_ok(c, in->b)) {
                sh_err(c, -1, "invalid matrix multiply");
                return -1;
            }
        }
        if (in->op == IR_CONST) {
            if (!temp_ok(c, in->dst) || in->aux < 0 || in->aux + in->ncomp > c->nimm) {
                sh_err(c, -1, "invalid constant");
                return -1;
            }
        }
        if (in->dst >= 0 && in->op != IR_LOAD_UNI && in->op != IR_MULMM && !temp_ok(c, in->dst)) {
            sh_err(c, -1, "invalid destination");
            return -1;
        }
        if (in->a >= 0 && !temp_ok(c, in->a)) {
            sh_err(c, -1, "invalid operand");
            return -1;
        }
        if (in->b >= 0 && !temp_ok(c, in->b)) {
            sh_err(c, -1, "invalid operand");
            return -1;
        }
    }
    if (sp != 0) {
        sh_err(c, -1, "unbalanced control flow");
        return -1;
    }
    return c->nerr ? -1 : 0;
}

int sh_opt(ShComp *c) {
    int use[SH_TEMP_MAX];
    int i;
    int pass;
    if (!c) {
        return -1;
    }
    for (pass = 0; pass < 4; ++pass) {
    memset(use, 0, sizeof use);
    for (i = 0; i < c->nir; ++i) {
        Ir *in = &c->ir[i];
        if (in->a >= 0 && in->a < c->ntmp) {
            use[in->a]++;
        }
        if (in->b >= 0 && in->b < c->ntmp) {
            use[in->b]++;
        }
        if (in->c >= 0 && in->c < c->ntmp) {
            use[in->c]++;
        }
        if (in->op == IR_SETLANE && in->dst >= 0 && in->dst < c->ntmp) {
            use[in->dst]++;
        }
    }
    for (i = 0; i < c->nir; ++i) {
        Ir *in = &c->ir[i];
        int pure = in->op == IR_CONST || in->op == IR_MOV || in->op == IR_SWZ ||
                   in->op == IR_ADD || in->op == IR_SUB || in->op == IR_MUL ||
                   in->op == IR_MAX || in->op == IR_MIN || in->op == IR_ABS ||
                   in->op == IR_DOT || in->op == IR_RSQ || in->op == IR_RCP ||
                   in->op == IR_SIN || in->op == IR_COS || in->op == IR_POW ||
                   in->op == IR_TRUNC || in->op == IR_CMP || in->op == IR_MULMV;
        if (!pure || in->dst < 0 || in->dst >= c->ntmp) {
            continue;
        }
        if (use[in->dst] == 0) {
            in->op = IR_NOP;
        }
    }
    }
    return 0;
}

static const char *op_name(int op) {
    switch (op) {
    case IR_CONST:
        return "const";
    case IR_MOV:
        return "mov";
    case IR_SWZ:
        return "swz";
    case IR_SETLANE:
        return "lane";
    case IR_ADD:
        return "add";
    case IR_SUB:
        return "sub";
    case IR_MUL:
        return "mul";
    case IR_MAX:
        return "max";
    case IR_MIN:
        return "min";
    case IR_ABS:
        return "abs";
    case IR_DOT:
        return "dot";
    case IR_RSQ:
        return "rsq";
    case IR_RCP:
        return "rcp";
    case IR_SIN:
        return "sin";
    case IR_COS:
        return "cos";
    case IR_POW:
        return "pow";
    case IR_TRUNC:
        return "trunc";
    case IR_CMP:
        return "cmp";
    case IR_MULMV:
        return "mul_mv";
    case IR_MULMM:
        return "mul_mm";
    case IR_SAMPLE:
        return "sample2d";
    case IR_LOAD_ATTR:
        return "load_attr";
    case IR_LOAD_VAR:
        return "load_var";
    case IR_LOAD_UNI:
        return "load_uni";
    case IR_LOAD_FCOORD:
        return "load_fragcoord";
    case IR_STORE_POS:
        return "store_pos";
    case IR_STORE_VAR:
        return "store_var";
    case IR_STORE_COLOR:
        return "store_color";
    case IR_IF:
        return "if";
    case IR_ELSE:
        return "else";
    case IR_ENDIF:
        return "endif";
    case IR_DISCARD:
        return "discard";
    default:
        return "nop";
    }
}

int sh_dump_ir_buf(const ShComp *c, char *dst, int cap) {
    int n = 0;
    int i;
    if (!c || !dst || cap < 2) {
        return -1;
    }
    dst[0] = 0;
    sh_app(dst, cap, &n, "shader ");
    sh_app(dst, cap, &n, c->name);
    sh_app(dst, cap, &n, "\nstage ");
    sh_app(dst, cap, &n, c->stage == SH_STAGE_VERTEX ? "vertex" : "fragment");
    sh_app(dst, cap, &n, "\n");
    for (i = 0; i < c->nsym; ++i) {
        const Sym *s = &c->sym[i];
        if (s->kind == SYM_UNIFORM) {
            sh_app(dst, cap, &n, "uniform ");
            sh_app(dst, cap, &n, sh_ty_name(s->ty));
            sh_app(dst, cap, &n, " ");
            sh_app(dst, cap, &n, s->name);
            sh_app(dst, cap, &n, " slot ");
            sh_app_i(dst, cap, &n, s->slot);
            sh_app(dst, cap, &n, "\n");
        } else if (s->kind == SYM_IN) {
            sh_app(dst, cap, &n, "input ");
            sh_app(dst, cap, &n, sh_ty_name(s->ty));
            sh_app(dst, cap, &n, " ");
            sh_app(dst, cap, &n, s->name);
            sh_app(dst, cap, &n, "\n");
        } else if (s->kind == SYM_OUT) {
            sh_app(dst, cap, &n, "output ");
            sh_app(dst, cap, &n, sh_ty_name(s->ty));
            sh_app(dst, cap, &n, " ");
            sh_app(dst, cap, &n, s->name);
            sh_app(dst, cap, &n, "\n");
        }
    }
    sh_app(dst, cap, &n, "block 0:\n");
    for (i = 0; i < c->nir; ++i) {
        const Ir *in = &c->ir[i];
        if (in->op == IR_NOP) {
            continue;
        }
        sh_app(dst, cap, &n, "  ");
        if (in->dst >= 0) {
            sh_app_ch(dst, cap, &n, '%');
            sh_app_i(dst, cap, &n, in->dst);
            sh_app(dst, cap, &n, " = ");
        }
        sh_app(dst, cap, &n, op_name(in->op));
        if (in->a >= 0) {
            sh_app(dst, cap, &n, " %");
            sh_app_i(dst, cap, &n, in->a);
        }
        if (in->b >= 0) {
            sh_app(dst, cap, &n, " %");
            sh_app_i(dst, cap, &n, in->b);
        }
        if (in->aux != 0 || in->op == IR_LOAD_ATTR || in->op == IR_LOAD_UNI ||
            in->op == IR_SAMPLE) {
            sh_app(dst, cap, &n, " aux ");
            sh_app_i(dst, cap, &n, in->aux);
        }
        sh_app(dst, cap, &n, "\n");
    }
    return n;
}

static const char *nk_name(int k) {
    switch (k) {
    case NK_DECL:
        return "decl";
    case NK_FUNC:
        return "func";
    case NK_BLOCK:
        return "block";
    case NK_RETURN:
        return "return";
    case NK_IF:
        return "if";
    case NK_FOR:
        return "for";
    case NK_ASSIGN:
        return "assign";
    case NK_CALL:
        return "call";
    case NK_CTOR:
        return "ctor";
    case NK_BINARY:
        return "binary";
    case NK_IDENT:
        return "ident";
    case NK_LIT:
        return "lit";
    case NK_SWZ:
        return "swz";
    default:
        return "node";
    }
}

int sh_dump_ast_buf(const ShComp *c, char *dst, int cap) {
    int n = 0;
    int i;
    if (!c || !dst || cap < 2) {
        return -1;
    }
    dst[0] = 0;
    sh_app(dst, cap, &n, "ast ");
    sh_app_i(dst, cap, &n, c->nast);
    sh_app(dst, cap, &n, "\n");
    for (i = 0; i < c->nast; ++i) {
        const Ast *a = &c->ast[i];
        sh_app_i(dst, cap, &n, i);
        sh_app(dst, cap, &n, " ");
        sh_app(dst, cap, &n, nk_name(a->kind));
        if (a->ty) {
            sh_app(dst, cap, &n, " ");
            sh_app(dst, cap, &n, sh_ty_name(a->ty));
        }
        if (a->tok >= 0 && a->tok < c->ntok && c->tok[a->tok].len) {
            int k;
            sh_app(dst, cap, &n, " ");
            for (k = 0; k < (int)c->tok[a->tok].len && k < 24; ++k) {
                sh_app_ch(dst, cap, &n, c->src[c->tok[a->tok].off + (uint32_t)k]);
            }
        }
        sh_app(dst, cap, &n, "\n");
    }
    return n;
}
