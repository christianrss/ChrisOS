#include "sh_int.h"

static ShVal vbad(void) {
    ShVal v;
    memset(&v, 0, sizeof v);
    v.tmp = -1;
    return v;
}

static int tok_str(const ShComp *c, int tok, char *dst, int cap) {
    int n;
    int i;
    if (tok < 0 || tok >= c->ntok || cap <= 0) {
        if (cap > 0) {
            dst[0] = 0;
        }
        return -1;
    }
    n = (int)c->tok[tok].len;
    if (n >= cap) {
        n = cap - 1;
    }
    for (i = 0; i < n; ++i) {
        dst[i] = c->src[c->tok[tok].off + (uint32_t)i];
    }
    dst[n] = 0;
    return n;
}

static int scope_has(const ShComp *c, int sc) {
    int s = c->cur_scope;
    int guard = 0;
    while (s >= 0 && guard++ < SH_SCOPE_MAX) {
        if (s == sc) {
            return 1;
        }
        if (s == 0) {
            break;
        }
        s = c->scope_parent[s];
    }
    return 0;
}

static void push_scope(ShComp *c) {
    int id;
    if (c->nscope + 1 >= SH_SCOPE_MAX) {
        sh_err(c, -1, "too many scopes");
        return;
    }
    id = ++c->nscope;
    c->scope_parent[id] = c->cur_scope;
    c->cur_scope = id;
}

static void pop_scope(ShComp *c) {
    if (c->cur_scope > 0) {
        c->cur_scope = c->scope_parent[c->cur_scope];
    }
}

static int lookup(ShComp *c, const char *name) {
    int i;
    for (i = c->nsym - 1; i >= 0; --i) {
        if (sh_eq(c->sym[i].name, name) && scope_has(c, c->sym[i].scope)) {
            return i;
        }
    }
    return -1;
}

static int add_sym(ShComp *c, const char *name, int kind, int ty) {
    Sym *s;
    int i;
    if (c->nsym >= SH_SYM_MAX) {
        sh_err(c, -1, "too many symbols");
        return -1;
    }
    for (i = 0; i < c->nsym; ++i) {
        if (c->sym[i].scope == c->cur_scope && sh_eq(c->sym[i].name, name)) {
            sh_err(c, -1, "duplicate symbol");
            return -1;
        }
    }
    s = &c->sym[c->nsym];
    memset(s, 0, sizeof *s);
    {
        int n = 0;
        while (name[n] && n + 1 < SH_NAME_MAX) {
            s->name[n] = name[n];
            ++n;
        }
        s->name[n] = 0;
    }
    s->kind = (uint8_t)kind;
    s->ty = (uint8_t)ty;
    s->loc = -1;
    s->slot = -1;
    s->scope = (int16_t)c->cur_scope;
    s->tmp = -1;
    s->body = -1;
    s->params = -1;
    return c->nsym++;
}

static int emit(ShComp *c, Ir in) {
    if (c->nir >= SH_IR_MAX) {
        sh_err(c, -1, "shader exceeds the instruction limit");
        return -1;
    }
    c->ir[c->nir++] = in;
    return 0;
}

static int alloc_tmp(ShComp *c, int n) {
    int id;
    if (n < 1) {
        n = 1;
    }
    if (c->ntmp + n > SH_TEMP_MAX) {
        sh_err(c, -1, "shader exceeds the temporary limit");
        return -1;
    }
    id = c->ntmp;
    c->ntmp += n;
    return id;
}

static int emit_const(ShComp *c, const float *v, int n, int *dst) {
    Ir in;
    int i;
    int id;
    if (c->nimm + n > SH_IMM_MAX) {
        sh_err(c, -1, "too many constants");
        return -1;
    }
    id = alloc_tmp(c, 1);
    if (id < 0) {
        return -1;
    }
    memset(&in, 0, sizeof in);
    in.op = IR_CONST;
    in.dst = (int16_t)id;
    in.aux = (int16_t)c->nimm;
    in.ncomp = (uint8_t)n;
    in.ty = (uint8_t)(n == 1 ? SH_TY_FLOAT : n == 2 ? SH_TY_VEC2 : n == 3 ? SH_TY_VEC3 : SH_TY_VEC4);
    for (i = 0; i < n; ++i) {
        c->imm[c->nimm++] = v[i];
    }
    if (emit(c, in) != 0) {
        return -1;
    }
    *dst = id;
    return 0;
}

static ShVal vconst(ShComp *c, int ty, const float *cv, int n) {
    ShVal v = vbad();
    int dst;
    if (emit_const(c, cv, n, &dst) != 0) {
        return v;
    }
    v.tmp = (int16_t)dst;
    v.ty = (uint8_t)ty;
    v.ncomp = (uint8_t)n;
    v.is_const = 1;
    {
        int i;
        for (i = 0; i < n && i < 4; ++i) {
            v.cv[i] = cv[i];
        }
    }
    return v;
}

static ShVal vscalar(ShComp *c, int ty, float x) {
    float a[1];
    a[0] = x;
    return vconst(c, ty, a, 1);
}

static int vec_ty(int n, int ivec) {
    if (ivec) {
        return n == 2 ? SH_TY_IVEC2 : n == 3 ? SH_TY_IVEC3 : SH_TY_IVEC4;
    }
    return n == 1 ? SH_TY_FLOAT : n == 2 ? SH_TY_VEC2 : n == 3 ? SH_TY_VEC3 : SH_TY_VEC4;
}

static ShVal vtmp(int tmp, int ty) {
    ShVal v = vbad();
    v.tmp = (int16_t)tmp;
    v.ty = (uint8_t)ty;
    v.ncomp = (uint8_t)sh_ncomp_ty(ty);
    if (ty == SH_TY_MAT3) {
        v.cols = 3;
        v.ncomp = 3;
    }
    if (ty == SH_TY_MAT4) {
        v.cols = 4;
        v.ncomp = 4;
    }
    return v;
}

static int emit_op(ShComp *c, int op, int ty, int dst, int a, int b, int cc, int aux, int aux2,
                   int ncomp) {
    Ir in;
    memset(&in, 0, sizeof in);
    in.op = (uint8_t)op;
    in.ty = (uint8_t)ty;
    in.dst = (int16_t)dst;
    in.a = (int16_t)a;
    in.b = (int16_t)b;
    in.c = (int16_t)cc;
    in.aux = (int16_t)aux;
    in.aux2 = (int16_t)aux2;
    in.ncomp = (uint8_t)ncomp;
    return emit(c, in);
}

static ShVal swizzle(ShComp *c, ShVal src, int mask, int ncomp) {
    int dst;
    int ty;
    ShVal v;
    int i;
    if (src.cols) {
        sh_err(c, -1, "cannot swizzle a matrix");
        return vbad();
    }
    for (i = 0; i < ncomp; ++i) {
        int comp = (mask >> (i * 2)) & 3;
        if (comp >= (int)src.ncomp) {
            sh_err(c, -1, "invalid swizzle");
            return vbad();
        }
    }
    if (src.is_const) {
        float cv[4];
        for (i = 0; i < ncomp; ++i) {
            int comp = (mask >> (i * 2)) & 3;
            cv[i] = src.cv[comp];
        }
        return vconst(c, vec_ty(ncomp, 0), cv, ncomp);
    }
    dst = alloc_tmp(c, 1);
    if (dst < 0) {
        return vbad();
    }
    ty = vec_ty(ncomp, src.ty >= SH_TY_IVEC2 && src.ty <= SH_TY_IVEC4);
    if (src.ty == SH_TY_INT || src.ty == SH_TY_IVEC2 || src.ty == SH_TY_IVEC3 ||
        src.ty == SH_TY_IVEC4) {
        ty = ncomp == 1 ? SH_TY_INT : vec_ty(ncomp, 1);
    }
    if (emit_op(c, IR_SWZ, ty, dst, src.tmp, -1, -1, mask, 0, ncomp) != 0) {
        return vbad();
    }
    v = vtmp(dst, ty);
    v.ncomp = (uint8_t)ncomp;
    return v;
}

static ShVal broadcast(ShComp *c, ShVal s, int ncomp) {
    if ((int)s.ncomp == ncomp) {
        return s;
    }
    if (s.ncomp != 1) {
        sh_err(c, -1, "cannot broadcast this value");
        return vbad();
    }
    return swizzle(c, s, 0, ncomp);
}

static ShVal expr(ShComp *c, int node);

static ShVal bin_arith(ShComp *c, int op, ShVal L, ShVal R, int tok) {
    int n;
    int ty;
    int dst;
    ShVal v;
    if (L.is_const && R.is_const && L.ncomp == R.ncomp && L.ncomp > 0 && !L.cols && !R.cols) {
        float cv[4];
        int i;
        for (i = 0; i < (int)L.ncomp; ++i) {
            float a = L.cv[i];
            float b = R.cv[i];
            if (op == IR_ADD) {
                cv[i] = a + b;
            } else if (op == IR_SUB) {
                cv[i] = a - b;
            } else if (op == IR_MUL) {
                cv[i] = a * b;
            } else if (op == IR_MIN) {
                cv[i] = a < b ? a : b;
            } else if (op == IR_MAX) {
                cv[i] = a > b ? a : b;
            } else {
                cv[i] = 0.f;
            }
        }
        return vconst(c, L.ncomp == 1 ? L.ty : vec_ty(L.ncomp, 0), cv, L.ncomp);
    }
    n = (int)L.ncomp;
    ty = L.ty;
    if (L.ncomp != R.ncomp) {
        if (L.ncomp == 1) {
            L = broadcast(c, L, R.ncomp);
            n = R.ncomp;
            ty = R.ty;
        } else if (R.ncomp == 1) {
            R = broadcast(c, R, L.ncomp);
            n = L.ncomp;
        } else {
            sh_err(c, tok, "type mismatch");
            return vbad();
        }
    }
    if (L.is_const && R.is_const && L.ncomp == R.ncomp && !L.cols && !R.cols) {
        float cv[4];
        int i;
        for (i = 0; i < (int)L.ncomp; ++i) {
            float a = L.cv[i];
            float b = R.cv[i];
            if (op == IR_ADD) {
                cv[i] = a + b;
            } else if (op == IR_SUB) {
                cv[i] = a - b;
            } else if (op == IR_MUL) {
                cv[i] = a * b;
            } else if (op == IR_MIN) {
                cv[i] = a < b ? a : b;
            } else if (op == IR_MAX) {
                cv[i] = a > b ? a : b;
            } else {
                cv[i] = 0.f;
            }
        }
        return vconst(c, L.ncomp == 1 ? L.ty : vec_ty((int)L.ncomp, 0), cv, (int)L.ncomp);
    }
    if (L.tmp < 0 || R.tmp < 0) {
        return vbad();
    }
    dst = alloc_tmp(c, 1);
    if (dst < 0) {
        return vbad();
    }
    if (emit_op(c, op, ty, dst, L.tmp, R.tmp, -1, 0, 0, n) != 0) {
        return vbad();
    }
    v = vtmp(dst, n == 1 ? (sh_is_scalar(ty) ? ty : SH_TY_FLOAT) : vec_ty(n, 0));
    if (!sh_is_scalar(ty) && n > 1) {
        v.ty = (uint8_t)vec_ty(n, ty == SH_TY_INT || (ty >= SH_TY_IVEC2 && ty <= SH_TY_IVEC4));
    }
    v.ncomp = (uint8_t)n;
    return v;
}

static ShVal mul_mv(ShComp *c, ShVal M, ShVal V) {
    int dst = alloc_tmp(c, 1);
    int cols = M.cols ? M.cols : (M.ty == SH_TY_MAT3 ? 3 : 4);
    int ty = cols == 3 ? SH_TY_VEC3 : SH_TY_VEC4;
    if (dst < 0) {
        return vbad();
    }
    if ((int)V.ncomp != cols) {
        sh_err(c, -1, "cannot multiply matrix by this vector");
        return vbad();
    }
    if (emit_op(c, IR_MULMV, ty, dst, M.tmp, V.tmp, -1, cols, 0, cols) != 0) {
        return vbad();
    }
    return vtmp(dst, ty);
}

static ShVal mul_mm(ShComp *c, ShVal A, ShVal B) {
    int cols = A.cols ? A.cols : 4;
    int dst = alloc_tmp(c, cols);
    int ty = cols == 3 ? SH_TY_MAT3 : SH_TY_MAT4;
    if (dst < 0) {
        return vbad();
    }
    if (A.cols != B.cols) {
        sh_err(c, -1, "matrix size mismatch");
        return vbad();
    }
    if (emit_op(c, IR_MULMM, ty, dst, A.tmp, B.tmp, -1, cols, 0, cols) != 0) {
        return vbad();
    }
    return vtmp(dst, ty);
}

static ShVal do_cmp(ShComp *c, int cmp, ShVal L, ShVal R, int tok) {
    int dst;
    if (!sh_is_scalar(L.ty) || !sh_is_scalar(R.ty)) {
        sh_err(c, tok, "vector comparisons are not supported");
        return vbad();
    }
    if (L.is_const && R.is_const) {
        int r = 0;
        float a = L.cv[0];
        float b = R.cv[0];
        if (cmp == CMP_LT) {
            r = a < b;
        } else if (cmp == CMP_GT) {
            r = a > b;
        } else if (cmp == CMP_LE) {
            r = a <= b;
        } else if (cmp == CMP_GE) {
            r = a >= b;
        } else if (cmp == CMP_EQ) {
            r = a == b;
        } else {
            r = a != b;
        }
        return vscalar(c, SH_TY_BOOL, r ? 1.f : 0.f);
    }
    dst = alloc_tmp(c, 1);
    if (dst < 0 || L.tmp < 0 || R.tmp < 0) {
        return vbad();
    }
    if (emit_op(c, IR_CMP, SH_TY_BOOL, dst, L.tmp, R.tmp, -1, cmp, 0, 1) != 0) {
        return vbad();
    }
    return vtmp(dst, SH_TY_BOOL);
}

static int gather_args(ShComp *c, int node, ShVal *out, int maxn) {
    int n = 0;
    while (node >= 0) {
        if (n >= maxn) {
            sh_err(c, c->ast[node].tok, "too many arguments");
            return -1;
        }
        out[n] = expr(c, node);
        if (out[n].ty == SH_TY_NONE && out[n].tmp < 0 && !out[n].samp) {
            return -1;
        }
        node = c->ast[node].next;
        ++n;
    }
    return n;
}

static ShVal compose(ShComp *c, int ty, ShVal *args, int nargs, int tok) {
    int need = sh_ncomp_ty(ty);
    int got = 0;
    int i;
    int dst;
    int lane;
    float zero[4];
    if (sh_is_mat(ty)) {
        int cols = ty == SH_TY_MAT3 ? 3 : 4;
        if (nargs == 1 && args[0].ty == SH_TY_FLOAT) {
            int base;
            int col;
            float diag = args[0].is_const ? args[0].cv[0] : 1.f;
            if (!args[0].is_const) {
                sh_err(c, tok, "matrix diagonal constructor needs a constant");
                return vbad();
            }
            base = alloc_tmp(c, cols);
            if (base < 0) {
                return vbad();
            }
            for (col = 0; col < cols; ++col) {
                float cv[4];
                int k;
                int one;
                for (k = 0; k < 4; ++k) {
                    cv[k] = 0.f;
                }
                cv[col] = diag;
                if (emit_const(c, cv, 4, &one) != 0) {
                    return vbad();
                }
                if (emit_op(c, IR_MOV, SH_TY_VEC4, base + col, one, -1, -1, 0, 0, 4) != 0) {
                    return vbad();
                }
            }
            return vtmp(base, ty);
        }
        if (nargs == cols) {
            int base = alloc_tmp(c, cols);
            if (base < 0) {
                return vbad();
            }
            for (i = 0; i < cols; ++i) {
                if ((int)args[i].ncomp != cols && (int)args[i].ncomp != 4) {
                    sh_err(c, tok, "bad matrix column");
                    return vbad();
                }
                if (emit_op(c, IR_MOV, SH_TY_VEC4, base + i, args[i].tmp, -1, -1, 0, 0,
                            args[i].ncomp) != 0) {
                    return vbad();
                }
            }
            return vtmp(base, ty);
        }
        sh_err(c, tok, "unsupported matrix constructor");
        return vbad();
    }
    if (nargs == 1 && (int)args[0].ncomp == 1 && need > 1) {
        return swizzle(c, args[0], 0, need);
    }
    if (nargs == 1 && (int)args[0].ncomp > need && !args[0].cols) {
        return swizzle(c, args[0], 0 | (1 << 2) | (2 << 4) | (3 << 6), need);
    }
    for (i = 0; i < nargs; ++i) {
        if (args[i].cols || args[i].samp) {
            sh_err(c, tok, "bad constructor argument");
            return vbad();
        }
        got += (int)args[i].ncomp;
    }
    if (got != need) {
        sh_err(c, tok, "constructor component count does not match the type");
        return vbad();
    }
    dst = alloc_tmp(c, 1);
    if (dst < 0) {
        return vbad();
    }
    zero[0] = zero[1] = zero[2] = zero[3] = 0.f;
    {
        int z;
        if (emit_const(c, zero, 4, &z) != 0) {
            return vbad();
        }
        if (emit_op(c, IR_MOV, ty, dst, z, -1, -1, 0, 0, 4) != 0) {
            return vbad();
        }
    }
    lane = 0;
    for (i = 0; i < nargs; ++i) {
        int k;
        for (k = 0; k < (int)args[i].ncomp; ++k) {
            if (emit_op(c, IR_SETLANE, ty, dst, args[i].tmp, -1, -1, lane, k, need) != 0) {
                return vbad();
            }
            ++lane;
        }
    }
    return vtmp(dst, ty);
}

static ShVal call_user(ShComp *c, int si, ShVal *args, int nargs, int tok);

static ShVal call_builtin(ShComp *c, const char *name, ShVal *a, int n, int tok) {
    if (sh_eq(name, "texture")) {
        int dst;
        if (n != 2 || !a[0].samp || a[1].ncomp < 2) {
            sh_err(c, tok, "texture() expects a sampler2D and a vec2");
            return vbad();
        }
        if (c->stage != SH_STAGE_FRAGMENT && c->stage != SH_STAGE_VERTEX) {
            sh_err(c, tok, "invalid stage for texture()");
            return vbad();
        }
        dst = alloc_tmp(c, 1);
        if (dst < 0) {
            return vbad();
        }
        if (emit_op(c, IR_SAMPLE, SH_TY_VEC4, dst, a[1].tmp, -1, -1, a[0].tmp, 0, 4) != 0) {
            return vbad();
        }
        return vtmp(dst, SH_TY_VEC4);
    }
    if (sh_eq(name, "dot")) {
        int dst;
        if (n != 2 || a[0].ncomp != a[1].ncomp || a[0].ncomp < 2 || a[0].cols) {
            sh_err(c, tok, "dot() expects two vectors of the same size");
            return vbad();
        }
        if (a[0].ncomp == 2) {
            ShVal p = bin_arith(c, IR_MUL, a[0], a[1], tok);
            ShVal x = swizzle(c, p, 0, 1);
            ShVal y = swizzle(c, p, 1, 1);
            return bin_arith(c, IR_ADD, x, y, tok);
        }
        dst = alloc_tmp(c, 1);
        if (dst < 0) {
            return vbad();
        }
        if (emit_op(c, IR_DOT, SH_TY_FLOAT, dst, a[0].tmp, a[1].tmp, -1, a[0].ncomp, 0, 1) != 0) {
            return vbad();
        }
        return vtmp(dst, SH_TY_FLOAT);
    }
    if (sh_eq(name, "cross")) {
        ShVal ay, az, by, bz, ax, bx;
        ShVal t0, t1, x, y, z;
        ShVal args[3];
        if (n != 2 || a[0].ty != SH_TY_VEC3 || a[1].ty != SH_TY_VEC3) {
            sh_err(c, tok, "cross() expects two vec3 values");
            return vbad();
        }
        ay = swizzle(c, a[0], 1, 1);
        az = swizzle(c, a[0], 2, 1);
        ax = swizzle(c, a[0], 0, 1);
        by = swizzle(c, a[1], 1, 1);
        bz = swizzle(c, a[1], 2, 1);
        bx = swizzle(c, a[1], 0, 1);
        t0 = bin_arith(c, IR_MUL, ay, bz, tok);
        t1 = bin_arith(c, IR_MUL, az, by, tok);
        x = bin_arith(c, IR_SUB, t0, t1, tok);
        t0 = bin_arith(c, IR_MUL, az, bx, tok);
        t1 = bin_arith(c, IR_MUL, ax, bz, tok);
        y = bin_arith(c, IR_SUB, t0, t1, tok);
        t0 = bin_arith(c, IR_MUL, ax, by, tok);
        t1 = bin_arith(c, IR_MUL, ay, bx, tok);
        z = bin_arith(c, IR_SUB, t0, t1, tok);
        args[0] = x;
        args[1] = y;
        args[2] = z;
        return compose(c, SH_TY_VEC3, args, 3, tok);
    }
    if (sh_eq(name, "length") || sh_eq(name, "normalize")) {
        ShVal d;
        int rdst;
        int ndst;
        if (n != 1 || a[0].ncomp < 2 || a[0].cols) {
            sh_err(c, tok, "expected a vector");
            return vbad();
        }
        if (a[0].ncomp == 2) {
            ShVal xx = swizzle(c, a[0], 0, 1);
            ShVal yy = swizzle(c, a[0], 1, 1);
            ShVal s0 = bin_arith(c, IR_MUL, xx, xx, tok);
            ShVal s1 = bin_arith(c, IR_MUL, yy, yy, tok);
            d = bin_arith(c, IR_ADD, s0, s1, tok);
        } else {
            int dst = alloc_tmp(c, 1);
            if (dst < 0) {
                return vbad();
            }
            if (emit_op(c, IR_DOT, SH_TY_FLOAT, dst, a[0].tmp, a[0].tmp, -1, a[0].ncomp, 0, 1) !=
                0) {
                return vbad();
            }
            d = vtmp(dst, SH_TY_FLOAT);
        }
        rdst = alloc_tmp(c, 1);
        if (rdst < 0) {
            return vbad();
        }
        if (emit_op(c, IR_RSQ, SH_TY_FLOAT, rdst, d.tmp, -1, -1, 0, 0, 1) != 0) {
            return vbad();
        }
        if (sh_eq(name, "length")) {
            ndst = alloc_tmp(c, 1);
            if (ndst < 0) {
                return vbad();
            }
            if (emit_op(c, IR_RCP, SH_TY_FLOAT, ndst, rdst, -1, -1, 0, 0, 1) != 0) {
                return vbad();
            }
            return vtmp(ndst, SH_TY_FLOAT);
        }
        {
            ShVal r = vtmp(rdst, SH_TY_FLOAT);
            r = broadcast(c, r, a[0].ncomp);
            return bin_arith(c, IR_MUL, a[0], r, tok);
        }
    }
    if (sh_eq(name, "abs") || sh_eq(name, "sin") || sh_eq(name, "cos")) {
        int op = sh_eq(name, "abs") ? IR_ABS : sh_eq(name, "sin") ? IR_SIN : IR_COS;
        int dst;
        if (n != 1 || a[0].cols || a[0].samp) {
            sh_err(c, tok, "wrong arguments");
            return vbad();
        }
        dst = alloc_tmp(c, 1);
        if (dst < 0) {
            return vbad();
        }
        if (emit_op(c, op, a[0].ty, dst, a[0].tmp, -1, -1, 0, 0, a[0].ncomp) != 0) {
            return vbad();
        }
        return vtmp(dst, a[0].ty);
    }
    if (sh_eq(name, "min") || sh_eq(name, "max")) {
        if (n != 2) {
            sh_err(c, tok, "wrong argument count");
            return vbad();
        }
        return bin_arith(c, sh_eq(name, "min") ? IR_MIN : IR_MAX, a[0], a[1], tok);
    }
    if (sh_eq(name, "clamp")) {
        ShVal hi;
        if (n != 3) {
            sh_err(c, tok, "clamp() expects 3 arguments");
            return vbad();
        }
        hi = bin_arith(c, IR_MAX, a[0], a[1], tok);
        return bin_arith(c, IR_MIN, hi, a[2], tok);
    }
    if (sh_eq(name, "mix")) {
        ShVal one;
        ShVal inv;
        ShVal p0, p1;
        if (n != 3) {
            sh_err(c, tok, "mix() expects 3 arguments");
            return vbad();
        }
        one = vscalar(c, SH_TY_FLOAT, 1.f);
        inv = bin_arith(c, IR_SUB, one, a[2], tok);
        p0 = bin_arith(c, IR_MUL, a[0], inv, tok);
        p1 = bin_arith(c, IR_MUL, a[1], a[2], tok);
        return bin_arith(c, IR_ADD, p0, p1, tok);
    }
    if (sh_eq(name, "pow")) {
        int dst;
        if (n != 2 || !sh_is_scalar(a[0].ty) || !sh_is_scalar(a[1].ty)) {
            sh_err(c, tok, "pow() expects two scalars");
            return vbad();
        }
        dst = alloc_tmp(c, 1);
        if (dst < 0) {
            return vbad();
        }
        if (emit_op(c, IR_POW, SH_TY_FLOAT, dst, a[0].tmp, a[1].tmp, -1, 0, 0, 1) != 0) {
            return vbad();
        }
        return vtmp(dst, SH_TY_FLOAT);
    }
    if (sh_eq(name, "reflect")) {
        ShVal d, two, sc, term;
        if (n != 2 || a[0].ncomp != a[1].ncomp || a[0].ncomp < 2) {
            sh_err(c, tok, "reflect() expects two vectors");
            return vbad();
        }
        d = call_builtin(c, "dot", a, 2, tok);
        two = vscalar(c, SH_TY_FLOAT, 2.f);
        sc = bin_arith(c, IR_MUL, two, d, tok);
        sc = broadcast(c, sc, a[1].ncomp);
        term = bin_arith(c, IR_MUL, sc, a[1], tok);
        return bin_arith(c, IR_SUB, a[0], term, tok);
    }
    {
        int si = lookup(c, name);
        if (si >= 0 && c->sym[si].kind == SYM_FUNC) {
            return call_user(c, si, a, n, tok);
        }
    }
    sh_err(c, tok, "unknown function");
    return vbad();
}

static void sema_stmt(ShComp *c, int node);

static ShVal call_user(ShComp *c, int si, ShVal *args, int nargs, int tok) {
    Sym *fn = &c->sym[si];
    int pnode;
    int i;
    int saved_ret;
    ShVal result;
    if (fn->ty == SH_TY_VOID) {
        sh_err(c, tok, "void function used as a value");
        return vbad();
    }
    if (nargs != (int)fn->nparam) {
        sh_err(c, tok, "wrong argument count");
        return vbad();
    }
    for (i = 0; i < c->fn_sp; ++i) {
        if (c->fn_stk[i] == si) {
            sh_err(c, tok, "recursion is not supported");
            return vbad();
        }
    }
    if (c->fn_sp >= 8) {
        sh_err(c, tok, "call depth limit");
        return vbad();
    }
    c->fn_stk[c->fn_sp++] = si;
    push_scope(c);
    pnode = fn->params;
    for (i = 0; i < nargs; ++i) {
        char pname[SH_NAME_MAX];
        int psi;
        int dst;
        if (pnode < 0) {
            break;
        }
        if (args[i].ty != fn->pty[i] &&
            !(sh_is_scalar(args[i].ty) && sh_is_scalar(fn->pty[i]))) {
            sh_err(c, tok, "argument type mismatch");
            pop_scope(c);
            c->fn_sp--;
            return vbad();
        }
        tok_str(c, c->ast[pnode].tok, pname, SH_NAME_MAX);
        psi = add_sym(c, pname, SYM_LOCAL, fn->pty[i]);
        dst = alloc_tmp(c, 1);
        if (psi < 0 || dst < 0) {
            pop_scope(c);
            c->fn_sp--;
            return vbad();
        }
        if (emit_op(c, IR_MOV, fn->pty[i], dst, args[i].tmp, -1, -1, 0, 0, args[i].ncomp) != 0) {
            pop_scope(c);
            c->fn_sp--;
            return vbad();
        }
        c->sym[psi].tmp = (int16_t)dst;
        pnode = c->ast[pnode].next;
    }
    saved_ret = c->ret_set;
    c->ret_set = 0;
    {
        int saved_depth = c->depth;
        c->depth = 0;
        sema_stmt(c, fn->body);
        c->depth = saved_depth;
    }
    if (!c->ret_set) {
        sh_err(c, tok, "function is missing a return value");
        result = vbad();
    } else {
        result = c->ret_val;
    }
    c->ret_set = saved_ret;
    pop_scope(c);
    c->fn_sp--;
    return result;
}

static ShVal expr(ShComp *c, int node) {
    Ast *a;
    if (node < 0 || node >= c->nast || c->nerr >= SH_ERR_MAX) {
        return vbad();
    }
    a = &c->ast[node];
    if (a->kind == NK_LIT) {
        if (a->op == 2) {
            return vscalar(c, SH_TY_BOOL, a->i ? 1.f : 0.f);
        }
        if (a->op == 1) {
            return vscalar(c, SH_TY_INT, (float)a->i);
        }
        return vscalar(c, SH_TY_FLOAT, a->f);
    }
    if (a->kind == NK_IDENT) {
        char name[SH_NAME_MAX];
        int si;
        Sym *s;
        tok_str(c, a->tok, name, SH_NAME_MAX);
        si = lookup(c, name);
        if (si < 0) {
            sh_err(c, a->tok, "unknown identifier");
            return vbad();
        }
        s = &c->sym[si];
        if (s->kind == SYM_POS && c->stage != SH_STAGE_VERTEX) {
            sh_err(c, a->tok, "gl_Position is not available in this stage");
            return vbad();
        }
        if (s->kind == SYM_FCOORD && c->stage != SH_STAGE_FRAGMENT) {
            sh_err(c, a->tok, "gl_FragCoord is not available in this stage");
            return vbad();
        }
        if (s->kind == SYM_FUNC) {
            sh_err(c, a->tok, "function used as a value");
            return vbad();
        }
        if (s->ty == SH_TY_SAMPLER2D) {
            ShVal v = vbad();
            v.ty = SH_TY_SAMPLER2D;
            v.samp = 1;
            v.tmp = s->slot;
            return v;
        }
        if (s->known && sh_is_scalar(s->ty)) {
            return vscalar(c, s->ty, s->cv);
        }
        if (s->tmp < 0) {
            sh_err(c, a->tok, "value used before assignment");
            return vbad();
        }
        return vtmp(s->tmp, s->ty);
    }
    if (a->kind == NK_SWZ) {
        ShVal base = expr(c, a->a);
        return swizzle(c, base, a->i, a->op);
    }
    if (a->kind == NK_INDEX) {
        ShVal base = expr(c, a->a);
        ShVal idx = expr(c, a->b);
        int lane;
        if (!idx.is_const || idx.ty != SH_TY_INT) {
            sh_err(c, a->tok, "only constant indexes are supported");
            return vbad();
        }
        lane = (int)idx.cv[0];
        if (lane < 0 || lane >= (int)base.ncomp) {
            sh_err(c, a->tok, "index out of range");
            return vbad();
        }
        return swizzle(c, base, lane, 1);
    }
    if (a->kind == NK_UNARY) {
        ShVal e = expr(c, a->a);
        if (a->op == TK_NOT) {
            ShVal one = vscalar(c, SH_TY_FLOAT, 1.f);
            return bin_arith(c, IR_SUB, one, e, a->tok);
        }
        if (a->op == TK_MINUS) {
            if (e.is_const) {
                float cv[4];
                int i;
                for (i = 0; i < (int)e.ncomp; ++i) {
                    cv[i] = -e.cv[i];
                }
                return vconst(c, e.ty, cv, e.ncomp);
            }
            {
                ShVal z = vscalar(c, SH_TY_FLOAT, 0.f);
                return bin_arith(c, IR_SUB, z, e, a->tok);
            }
        }
        return e;
    }
    if (a->kind == NK_BINARY) {
        ShVal L = expr(c, a->a);
        ShVal R = expr(c, a->b);
        if (L.ty == SH_TY_NONE || R.ty == SH_TY_NONE) {
            return vbad();
        }
        if (a->op == TK_STAR && sh_is_mat(L.ty) && (sh_is_vec(R.ty) || sh_is_scalar(R.ty))) {
            if (sh_is_scalar(R.ty)) {
                sh_err(c, a->tok, "cannot multiply a matrix by a scalar");
                return vbad();
            }
            return mul_mv(c, L, R);
        }
        if (a->op == TK_STAR && sh_is_mat(L.ty) && sh_is_mat(R.ty)) {
            return mul_mm(c, L, R);
        }
        if (a->op == TK_PLUS) {
            return bin_arith(c, IR_ADD, L, R, a->tok);
        }
        if (a->op == TK_MINUS) {
            return bin_arith(c, IR_SUB, L, R, a->tok);
        }
        if (a->op == TK_STAR) {
            return bin_arith(c, IR_MUL, L, R, a->tok);
        }
        if (a->op == TK_SLASH) {
            ShVal inv;
            int dst;
            if (R.is_const && R.cv[0] == 0.f && R.ncomp == 1) {
                sh_err(c, a->tok, "division by zero");
                return vbad();
            }
            dst = alloc_tmp(c, 1);
            if (dst < 0) {
                return vbad();
            }
            if (emit_op(c, IR_RCP, SH_TY_FLOAT, dst, R.tmp, -1, -1, 0, 0, R.ncomp) != 0) {
                return vbad();
            }
            inv = vtmp(dst, R.ncomp == 1 ? SH_TY_FLOAT : R.ty);
            inv.ncomp = R.ncomp;
            return bin_arith(c, IR_MUL, L, inv, a->tok);
        }
        if (a->op == TK_PERCENT) {
            if (L.is_const && R.is_const && R.cv[0] != 0.f) {
                int ai = (int)L.cv[0];
                int bi = (int)R.cv[0];
                return vscalar(c, SH_TY_INT, (float)(ai % bi));
            }
            sh_err(c, a->tok, "operator % is only supported on constant integers");
            return vbad();
        }
        if (a->op == TK_LT) {
            return do_cmp(c, CMP_LT, L, R, a->tok);
        }
        if (a->op == TK_GT) {
            return do_cmp(c, CMP_GT, L, R, a->tok);
        }
        if (a->op == TK_LE) {
            return do_cmp(c, CMP_LE, L, R, a->tok);
        }
        if (a->op == TK_GE) {
            return do_cmp(c, CMP_GE, L, R, a->tok);
        }
        if (a->op == TK_EQ) {
            return do_cmp(c, CMP_EQ, L, R, a->tok);
        }
        if (a->op == TK_NE) {
            return do_cmp(c, CMP_NE, L, R, a->tok);
        }
        if (a->op == TK_AND || a->op == TK_OR) {
            if (a->op == TK_AND) {
                return bin_arith(c, IR_MUL, L, R, a->tok);
            }
            return bin_arith(c, IR_MAX, L, R, a->tok);
        }
        sh_err(c, a->tok, "unsupported operator");
        return vbad();
    }
    if (a->kind == NK_CALL) {
        char name[SH_NAME_MAX];
        ShVal args[8];
        int n;
        tok_str(c, a->tok, name, SH_NAME_MAX);
        n = gather_args(c, a->a, args, 8);
        if (n < 0) {
            return vbad();
        }
        return call_builtin(c, name, args, n, a->tok);
    }
    if (a->kind == NK_CTOR) {
        ShVal args[16];
        int n = gather_args(c, a->a, args, 16);
        if (n < 0) {
            return vbad();
        }
        if ((a->ty == SH_TY_FLOAT || a->ty == SH_TY_INT) && n == 1) {
            if (a->ty == SH_TY_INT && args[0].ty == SH_TY_FLOAT) {
                int dst = alloc_tmp(c, 1);
                if (dst < 0) {
                    return vbad();
                }
                if (emit_op(c, IR_TRUNC, SH_TY_INT, dst, args[0].tmp, -1, -1, 0, 0, 1) != 0) {
                    return vbad();
                }
                return vtmp(dst, SH_TY_INT);
            }
            return vtmp(args[0].tmp, a->ty);
        }
        return compose(c, a->ty, args, n, a->tok);
    }
    if (a->kind == NK_POST || a->kind == NK_PRE) {
        sh_err(c, a->tok, "increment is only supported as a loop step");
        return vbad();
    }
    sh_err(c, a->tok, "unsupported expression");
    return vbad();
}

static int assignable(ShComp *c, int node, int *si) {
    char name[SH_NAME_MAX];
    if (node < 0 || c->ast[node].kind != NK_IDENT) {
        sh_err(c, node >= 0 ? c->ast[node].tok : -1, "assignment target must be a name");
        return -1;
    }
    tok_str(c, c->ast[node].tok, name, SH_NAME_MAX);
    *si = lookup(c, name);
    if (*si < 0) {
        sh_err(c, c->ast[node].tok, "unknown identifier");
        return -1;
    }
    return 0;
}

static void store_sym(ShComp *c, int si, ShVal v, int tok) {
    Sym *s = &c->sym[si];
    if (s->is_const) {
        sh_err(c, tok, "cannot assign to const");
        return;
    }
    if (s->ty == SH_TY_SAMPLER2D || s->kind == SYM_FUNC || s->kind == SYM_FCOORD) {
        sh_err(c, tok, "invalid assignment");
        return;
    }
    if (s->kind == SYM_UNIFORM || s->kind == SYM_IN) {
        sh_err(c, tok, "invalid assignment");
        return;
    }
    if (v.ty != s->ty && !(sh_is_scalar(v.ty) && sh_is_scalar(s->ty))) {
        sh_err(c, tok, "type mismatch in assignment");
        return;
    }
    if (s->kind == SYM_POS && v.ty != SH_TY_VEC4) {
        sh_err(c, tok, "gl_Position must be a vec4");
        return;
    }
    if (s->tmp < 0) {
        int t = alloc_tmp(c, s->ty == SH_TY_MAT4 ? 4 : s->ty == SH_TY_MAT3 ? 3 : 1);
        if (t < 0) {
            return;
        }
        s->tmp = (int16_t)t;
    }
    if (v.tmp != s->tmp) {
        if (sh_is_mat(s->ty)) {
            int cols = s->ty == SH_TY_MAT3 ? 3 : 4;
            int i;
            for (i = 0; i < cols; ++i) {
                emit_op(c, IR_MOV, SH_TY_VEC4, s->tmp + i, v.tmp + i, -1, -1, 0, 0, 4);
            }
        } else if (emit_op(c, IR_MOV, s->ty, s->tmp, v.tmp, -1, -1, 0, 0, sh_ncomp_ty(s->ty)) !=
                   0) {
            return;
        }
    }
    s->known = v.is_const && sh_is_scalar(v.ty);
    if (s->known) {
        s->cv = v.cv[0];
    }
    s->wrote = 1;
    if (s->kind == SYM_POS) {
        emit_op(c, IR_STORE_POS, SH_TY_VEC4, -1, s->tmp, -1, -1, 0, 0, 4);
        c->wrote_pos = 1;
    } else if (s->kind == SYM_OUT && c->stage == SH_STAGE_VERTEX) {
        emit_op(c, IR_STORE_VAR, s->ty, -1, s->tmp, -1, -1, s->slot, 0, sh_ncomp_ty(s->ty));
    } else if (s->kind == SYM_OUT && c->stage == SH_STAGE_FRAGMENT) {
        emit_op(c, IR_STORE_COLOR, s->ty, -1, s->tmp, -1, -1, 0, 0, 4);
    }
}

static void sema_decl(ShComp *c, int node) {
    Ast *a = &c->ast[node];
    char name[SH_NAME_MAX];
    int si;
    tok_str(c, a->tok, name, SH_NAME_MAX);
    if (a->ty == SH_TY_VOID) {
        sh_err(c, a->tok, "void variable");
        return;
    }
    if ((a->quals & Q_UNIFORM) && a->ty != SH_TY_SAMPLER2D && a->a >= 0) {
        sh_err(c, a->tok, "uniforms cannot have an initializer");
        return;
    }
    if (((a->quals & Q_IN) || (a->quals & Q_OUT)) && a->a >= 0) {
        sh_err(c, a->tok, "shader inputs and outputs cannot have an initializer");
        return;
    }
    si = add_sym(c, name, SYM_LOCAL, a->ty);
    if (si < 0) {
        return;
    }
    if (a->quals & Q_CONST) {
        c->sym[si].is_const = 1;
    }
    {
        int nvec = a->ty == SH_TY_MAT4 ? 4 : a->ty == SH_TY_MAT3 ? 3 : 1;
        int t = alloc_tmp(c, nvec);
        if (t < 0) {
            return;
        }
        c->sym[si].tmp = (int16_t)t;
    }
    if (a->a >= 0) {
        ShVal v = expr(c, a->a);
        int locked = c->sym[si].is_const;
        c->sym[si].is_const = 0;
        store_sym(c, si, v, a->tok);
        c->sym[si].is_const = (uint8_t)locked;
        if (locked && v.is_const && sh_is_scalar(v.ty)) {
            c->sym[si].known = 1;
            c->sym[si].cv = v.cv[0];
        }
    }
}

static int eval_int(ShComp *c, int node, const char *ivar, int ival, int *out) {
    Ast *a;
    if (node < 0) {
        return -1;
    }
    a = &c->ast[node];
    if (a->kind == NK_LIT && a->op == 1) {
        *out = a->i;
        return 0;
    }
    if (a->kind == NK_IDENT) {
        char name[SH_NAME_MAX];
        tok_str(c, a->tok, name, SH_NAME_MAX);
        if (sh_eq(name, ivar)) {
            *out = ival;
            return 0;
        }
        return -1;
    }
    if (a->kind == NK_UNARY && a->op == TK_MINUS) {
        int v;
        if (eval_int(c, a->a, ivar, ival, &v) != 0) {
            return -1;
        }
        *out = -v;
        return 0;
    }
    if (a->kind == NK_BINARY && (a->op == TK_PLUS || a->op == TK_MINUS)) {
        int L, R;
        if (eval_int(c, a->a, ivar, ival, &L) != 0 || eval_int(c, a->b, ivar, ival, &R) != 0) {
            return -1;
        }
        *out = a->op == TK_PLUS ? L + R : L - R;
        return 0;
    }
    return -1;
}

static int eval_cond(ShComp *c, int node, const char *ivar, int ival, int *out) {
    Ast *a;
    int L, R;
    if (node < 0 || c->ast[node].kind != NK_BINARY) {
        return -1;
    }
    a = &c->ast[node];
    if (eval_int(c, a->a, ivar, ival, &L) != 0 || eval_int(c, a->b, ivar, ival, &R) != 0) {
        return -1;
    }
    if (a->op == TK_LT) {
        *out = L < R;
    } else if (a->op == TK_GT) {
        *out = L > R;
    } else if (a->op == TK_LE) {
        *out = L <= R;
    } else if (a->op == TK_GE) {
        *out = L >= R;
    } else if (a->op == TK_EQ) {
        *out = L == R;
    } else if (a->op == TK_NE) {
        *out = L != R;
    } else {
        return -1;
    }
    return 0;
}

static int step_int(ShComp *c, int node, const char *ivar, int *val) {
    Ast *a;
    if (node < 0) {
        return -1;
    }
    a = &c->ast[node];
    if ((a->kind == NK_POST || a->kind == NK_PRE) && a->a >= 0 && c->ast[a->a].kind == NK_IDENT) {
        char name[SH_NAME_MAX];
        tok_str(c, c->ast[a->a].tok, name, SH_NAME_MAX);
        if (!sh_eq(name, ivar)) {
            return -1;
        }
        if (a->op == TK_INC) {
            *val += 1;
        } else if (a->op == TK_DEC) {
            *val -= 1;
        } else {
            return -1;
        }
        return 0;
    }
    if (a->kind == NK_ASSIGN && c->ast[a->a].kind == NK_IDENT) {
        char name[SH_NAME_MAX];
        int v;
        tok_str(c, c->ast[a->a].tok, name, SH_NAME_MAX);
        if (!sh_eq(name, ivar)) {
            return -1;
        }
        if (eval_int(c, a->b, ivar, *val, &v) != 0) {
            return -1;
        }
        *val = v;
        return 0;
    }
    return -1;
}

static void sema_for(ShComp *c, int node) {
    Ast *a = &c->ast[node];
    char ivar[SH_NAME_MAX];
    int initv = 0;
    int si;
    int trip = 0;
    int guard = 0;
    if (a->a < 0 || c->ast[a->a].kind != NK_DECL || c->ast[a->a].ty != SH_TY_INT) {
        sh_err(c, a->tok, "unsupported loop");
        return;
    }
    tok_str(c, c->ast[a->a].tok, ivar, SH_NAME_MAX);
    if (c->ast[a->a].a < 0 || eval_int(c, c->ast[a->a].a, ivar, 0, &initv) != 0) {
        sh_err(c, a->tok, "loop initializer is not a constant");
        return;
    }
    push_scope(c);
    si = add_sym(c, ivar, SYM_LOCAL, SH_TY_INT);
    if (si < 0) {
        pop_scope(c);
        return;
    }
    while (guard++ < 8) {
        int cond = 0;
        int t;
        if (eval_cond(c, a->b, ivar, initv, &cond) != 0) {
            sh_err(c, a->tok, "unsupported loop condition");
            pop_scope(c);
            return;
        }
        if (!cond) {
            break;
        }
        t = alloc_tmp(c, 1);
        if (t < 0) {
            pop_scope(c);
            return;
        }
        {
            float cv = (float)initv;
            int cst;
            if (emit_const(c, &cv, 1, &cst) != 0) {
                pop_scope(c);
                return;
            }
            emit_op(c, IR_MOV, SH_TY_INT, t, cst, -1, -1, 0, 0, 1);
        }
        c->sym[si].tmp = (int16_t)t;
        c->sym[si].known = 1;
        c->sym[si].wrote = 0;
        c->sym[si].cv = (float)initv;
        sema_stmt(c, a->d);
        if (c->sym[si].wrote) {
            sh_err(c, a->tok, "cannot assign the loop variable");
            pop_scope(c);
            return;
        }
        if (step_int(c, a->c, ivar, &initv) != 0) {
            sh_err(c, a->tok, "unsupported loop step");
            pop_scope(c);
            return;
        }
        ++trip;
    }
    if (guard >= 8) {
        int cond = 0;
        if (eval_cond(c, a->b, ivar, initv, &cond) == 0 && cond) {
            sh_err(c, a->tok, "loop bound exceeds the unroll limit");
        }
    }
    (void)trip;
    pop_scope(c);
}

static void sema_stmt(ShComp *c, int node) {
    while (node >= 0 && c->nerr < SH_ERR_MAX) {
        Ast *a = &c->ast[node];
        if (a->kind == NK_EMPTY || a->kind == NK_FUNC) {
            node = a->next;
            continue;
        }
        if (a->kind == NK_BLOCK) {
            push_scope(c);
            c->depth++;
            sema_stmt(c, a->a);
            c->depth--;
            pop_scope(c);
        } else if (a->kind == NK_DECL) {
            if (a->quals & (Q_IN | Q_OUT | Q_UNIFORM)) {
                sh_err(c, a->tok, "qualifiers are only valid at global scope");
            } else {
                sema_decl(c, node);
            }
        } else if (a->kind == NK_ASSIGN) {
            int si = -1;
            ShVal v;
            if (assignable(c, a->a, &si) == 0) {
                v = expr(c, a->b);
                store_sym(c, si, v, a->tok);
            }
        } else if (a->kind == NK_EXPR) {
            (void)expr(c, a->a);
        } else if (a->kind == NK_RETURN) {
            if (c->depth != 1 || a->next >= 0) {
                sh_err(c, a->tok, "return must be the last statement of the function");
            }
            if (a->a >= 0) {
                c->ret_val = expr(c, a->a);
                c->ret_set = 1;
            } else {
                c->ret_set = 1;
                c->ret_val = vbad();
                c->ret_val.ty = SH_TY_VOID;
            }
        } else if (a->kind == NK_DISCARD) {
            if (c->stage != SH_STAGE_FRAGMENT) {
                sh_err(c, a->tok, "discard is only valid in a fragment shader");
            } else {
                Ir in;
                memset(&in, 0, sizeof in);
                in.op = IR_DISCARD;
                in.a = -1;
                in.b = -1;
                in.c = -1;
                in.dst = -1;
                emit(c, in);
            }
        } else if (a->kind == NK_IF) {
            ShVal cond = expr(c, a->a);
            Ir in;
            if (cond.ty != SH_TY_BOOL && !sh_is_scalar(cond.ty)) {
                sh_err(c, a->tok, "if condition must be a scalar");
            }
            memset(&in, 0, sizeof in);
            in.op = IR_IF;
            in.a = cond.tmp;
            in.b = -1;
            in.c = -1;
            in.dst = -1;
            emit(c, in);
            sema_stmt(c, a->b);
            if (a->c >= 0) {
                memset(&in, 0, sizeof in);
                in.op = IR_ELSE;
                in.a = in.b = in.c = in.dst = -1;
                emit(c, in);
                sema_stmt(c, a->c);
            }
            memset(&in, 0, sizeof in);
            in.op = IR_ENDIF;
            in.a = in.b = in.c = in.dst = -1;
            emit(c, in);
        } else if (a->kind == NK_FOR) {
            sema_for(c, node);
        } else {
            sh_err(c, a->tok, "unsupported statement");
        }
        node = a->next;
    }
}

static int uni_nvec(int ty) {
    if (ty == SH_TY_MAT4) {
        return 4;
    }
    if (ty == SH_TY_MAT3) {
        return 3;
    }
    if (ty == SH_TY_SAMPLER2D) {
        return 0;
    }
    return 1;
}

static void bind_slots(ShComp *c) {
    int i;
    int used_a[8];
    int used_v[8];
    int attr_next = 0;
    int var_next = 0;
    int uni = 0;
    int samp = 0;
    memset(used_a, 0, sizeof used_a);
    memset(used_v, 0, sizeof used_v);
    for (i = 0; i < c->nsym; ++i) {
        Sym *s = &c->sym[i];
        if (s->kind == SYM_IN && c->stage == SH_STAGE_VERTEX && s->loc >= 0) {
            if (s->loc >= 8 || used_a[s->loc]) {
                sh_err(c, -1, "attribute location conflict");
                return;
            }
            used_a[s->loc] = 1;
        }
        if (((s->kind == SYM_OUT && c->stage == SH_STAGE_VERTEX) ||
             (s->kind == SYM_IN && c->stage == SH_STAGE_FRAGMENT)) &&
            s->loc >= 0) {
            if (s->loc >= 8 || used_v[s->loc]) {
                sh_err(c, -1, "location conflict");
                return;
            }
            used_v[s->loc] = 1;
            s->slot = s->loc;
        }
    }
    for (i = 0; i < c->nsym; ++i) {
        Sym *s = &c->sym[i];
        if (s->kind == SYM_IN && c->stage == SH_STAGE_VERTEX) {
            if (s->loc < 0) {
                while (attr_next < 8 && used_a[attr_next]) {
                    ++attr_next;
                }
                if (attr_next >= 8) {
                    sh_err(c, -1, "too many attributes");
                    return;
                }
                s->loc = (int8_t)attr_next;
                used_a[attr_next] = 1;
            }
            if (s->loc > c->attr_hi) {
                c->attr_hi = s->loc;
            }
        }
        if ((s->kind == SYM_OUT && c->stage == SH_STAGE_VERTEX) ||
            (s->kind == SYM_IN && c->stage == SH_STAGE_FRAGMENT)) {
            if (s->slot < 0) {
                while (var_next < 8 && used_v[var_next]) {
                    ++var_next;
                }
                if (var_next >= 8) {
                    sh_err(c, -1, "too many varyings");
                    return;
                }
                s->slot = (int16_t)var_next;
                used_v[var_next] = 1;
            }
            if (s->slot > c->var_hi) {
                c->var_hi = s->slot;
            }
        }
        if (s->kind == SYM_UNIFORM) {
            if (s->ty == SH_TY_SAMPLER2D) {
                if (samp >= 4) {
                    sh_err(c, -1, "too many samplers");
                    return;
                }
                s->slot = (int16_t)samp++;
                c->samp_hi = samp - 1;
            } else {
                int n = uni_nvec(s->ty);
                s->slot = (int16_t)uni;
                uni += n;
                if (uni > 32) {
                    sh_err(c, -1, "too many uniforms");
                    return;
                }
                c->uni_hi = uni - 1;
            }
        }
        if (s->kind == SYM_OUT && c->stage == SH_STAGE_FRAGMENT) {
            c->nout++;
            if (s->ty != SH_TY_VEC4) {
                sh_err(c, -1, "fragment output must be vec4");
                return;
            }
            if (c->nout > 1) {
                sh_err(c, -1, "only one fragment output is supported");
                return;
            }
        }
    }
}

static void prologue(ShComp *c) {
    int i;
    for (i = 0; i < c->nsym; ++i) {
        Sym *s = &c->sym[i];
        if (s->kind == SYM_UNIFORM && s->ty != SH_TY_SAMPLER2D) {
            int n = uni_nvec(s->ty);
            int t = alloc_tmp(c, n);
            if (t < 0) {
                return;
            }
            s->tmp = (int16_t)t;
            emit_op(c, IR_LOAD_UNI, s->ty, t, -1, -1, -1, s->slot, n, n == 1 ? sh_ncomp_ty(s->ty) : 4);
        } else if (s->kind == SYM_IN && c->stage == SH_STAGE_VERTEX) {
            int t = alloc_tmp(c, 1);
            if (t < 0) {
                return;
            }
            s->tmp = (int16_t)t;
            emit_op(c, IR_LOAD_ATTR, s->ty, t, -1, -1, -1, s->loc, 0, 4);
        } else if (s->kind == SYM_IN && c->stage == SH_STAGE_FRAGMENT) {
            int t = alloc_tmp(c, 1);
            if (t < 0) {
                return;
            }
            s->tmp = (int16_t)t;
            emit_op(c, IR_LOAD_VAR, s->ty, t, -1, -1, -1, s->slot, 0, sh_ncomp_ty(s->ty));
        } else if (s->kind == SYM_OUT || (s->kind == SYM_POS && c->stage == SH_STAGE_VERTEX)) {
            int t = alloc_tmp(c, 1);
            float z[4] = {0, 0, 0, 0};
            int cst;
            if (t < 0 || emit_const(c, z, 4, &cst) != 0) {
                return;
            }
            s->tmp = (int16_t)t;
            emit_op(c, IR_MOV, SH_TY_VEC4, t, cst, -1, -1, 0, 0, 4);
        } else if (s->kind == SYM_FCOORD && c->stage == SH_STAGE_FRAGMENT) {
            int t = alloc_tmp(c, 1);
            if (t < 0) {
                return;
            }
            s->tmp = (int16_t)t;
            emit_op(c, IR_LOAD_FCOORD, SH_TY_VEC4, t, -1, -1, -1, 0, 0, 4);
        }
    }
}

static void reg_globals(ShComp *c, int node) {
    while (node >= 0) {
        Ast *a = &c->ast[node];
        char name[SH_NAME_MAX];
        if (a->kind == NK_DECL) {
            int kind = SYM_LOCAL;
            tok_str(c, a->tok, name, SH_NAME_MAX);
            if (a->quals & Q_IN) {
                kind = SYM_IN;
            } else if (a->quals & Q_OUT) {
                kind = SYM_OUT;
            } else if (a->quals & Q_UNIFORM) {
                kind = SYM_UNIFORM;
            }
            if (a->ty == SH_TY_SAMPLER2D && kind != SYM_UNIFORM) {
                sh_err(c, a->tok, "sampler2D must be a uniform");
            } else {
                int si = add_sym(c, name, kind, a->ty);
                if (si >= 0) {
                    c->sym[si].loc = (int8_t)(a->i > 127 ? 127 : a->i);
                    if (a->quals & Q_CONST) {
                        c->sym[si].is_const = 1;
                    }
                }
            }
        } else if (a->kind == NK_FUNC) {
            int si;
            int p;
            int n = 0;
            tok_str(c, a->tok, name, SH_NAME_MAX);
            si = add_sym(c, name, SYM_FUNC, a->ty);
            if (si >= 0) {
                c->sym[si].body = a->a;
                c->sym[si].params = a->b;
                for (p = a->b; p >= 0 && n < 4; p = c->ast[p].next) {
                    c->sym[si].pty[n++] = c->ast[p].ty;
                }
                if (p >= 0) {
                    sh_err(c, a->tok, "too many parameters");
                }
                c->sym[si].nparam = (uint8_t)n;
            }
        }
        node = a->next;
    }
}

int sh_sem(ShComp *c) {
    int main_si;
    int i;
    if (!c) {
        return -1;
    }
    c->cur_scope = 0;
    c->scope_parent[0] = -1;
    c->attr_hi = c->var_hi = c->uni_hi = c->samp_hi = -1;
    if (add_sym(c, "gl_Position", SYM_POS, SH_TY_VEC4) < 0) {
        return -1;
    }
    if (add_sym(c, "gl_FragCoord", SYM_FCOORD, SH_TY_VEC4) < 0) {
        return -1;
    }
    reg_globals(c, c->root);
    bind_slots(c);
    main_si = -1;
    for (i = 0; i < c->nsym; ++i) {
        if (c->sym[i].kind == SYM_FUNC && sh_eq(c->sym[i].name, "main")) {
            main_si = i;
        }
    }
    if (main_si < 0) {
        sh_err(c, -1, "missing main");
        return -1;
    }
    if (c->sym[main_si].ty != SH_TY_VOID || c->sym[main_si].nparam != 0) {
        sh_err(c, -1, "main must be void main()");
        return -1;
    }
    push_scope(c);
    prologue(c);
    c->fn_sp = 1;
    c->fn_stk[0] = main_si;
    c->depth = 0;
    sema_stmt(c, c->sym[main_si].body);
    c->fn_sp = 0;
    pop_scope(c);
    if (c->stage == SH_STAGE_VERTEX && !c->wrote_pos) {
        sh_err(c, -1, "vertex shader does not write gl_Position");
    }
    if (c->stage == SH_STAGE_FRAGMENT && c->nout < 1) {
        sh_err(c, -1, "fragment shader has no output");
    }
    return c->nerr ? -1 : 0;
}
