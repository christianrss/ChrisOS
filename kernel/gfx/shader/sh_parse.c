#include "sh_int.h"

static int peek(const ShComp *c) {
    if (c->tp >= c->ntok) {
        return TK_EOF;
    }
    return c->tok[c->tp].kind;
}

static int at(const ShComp *c, int k) {
    return peek(c) == k;
}

static void adv(ShComp *c) {
    if (c->tp < c->ntok && c->tok[c->tp].kind != TK_EOF) {
        c->tp++;
    }
}

static int node_new(ShComp *c, int kind, int tok) {
    Ast *a;
    if (c->nast >= SH_AST_MAX) {
        sh_err(c, tok, "shader exceeds the AST node limit");
        return -1;
    }
    a = &c->ast[c->nast];
    memset(a, 0, sizeof *a);
    a->kind = (uint8_t)kind;
    a->tok = (int16_t)tok;
    a->a = a->b = a->c = a->d = a->next = -1;
    a->i = -1;
    return c->nast++;
}

static int enter(ShComp *c) {
    if (c->depth >= SH_NEST_MAX) {
        sh_err(c, c->tp < c->ntok ? c->tp : c->ntok - 1, "nesting limit exceeded");
        return -1;
    }
    c->depth++;
    return 0;
}

static void leave(ShComp *c) {
    if (c->depth > 0) {
        c->depth--;
    }
}

static int type_of_tok(int k) {
    switch (k) {
    case TK_VOID:
        return SH_TY_VOID;
    case TK_BOOL:
        return SH_TY_BOOL;
    case TK_INTKW:
        return SH_TY_INT;
    case TK_FLOATKW:
        return SH_TY_FLOAT;
    case TK_VEC2:
        return SH_TY_VEC2;
    case TK_VEC3:
        return SH_TY_VEC3;
    case TK_VEC4:
        return SH_TY_VEC4;
    case TK_IVEC2:
        return SH_TY_IVEC2;
    case TK_IVEC3:
        return SH_TY_IVEC3;
    case TK_IVEC4:
        return SH_TY_IVEC4;
    case TK_MAT3:
        return SH_TY_MAT3;
    case TK_MAT4:
        return SH_TY_MAT4;
    case TK_SAMPLER2D:
        return SH_TY_SAMPLER2D;
    default:
        return 0;
    }
}

static int is_type_tok(int k) {
    return type_of_tok(k) != 0;
}

static void sync_stmt(ShComp *c) {
    int depth = 0;
    while (!at(c, TK_EOF)) {
        if (at(c, TK_LBRACE)) {
            depth++;
        } else if (at(c, TK_RBRACE)) {
            if (depth == 0) {
                break;
            }
            depth--;
            adv(c);
            if (depth == 0) {
                break;
            }
            continue;
        } else if (at(c, TK_SEMI) && depth == 0) {
            adv(c);
            break;
        }
        adv(c);
    }
}

static int parse_expr(ShComp *c);

static int parse_args(ShComp *c, int *first) {
    int head = -1;
    int tail = -1;
    *first = -1;
    if (at(c, TK_RPAREN)) {
        return 0;
    }
    for (;;) {
        int e = parse_expr(c);
        if (e < 0) {
            return -1;
        }
        if (head < 0) {
            head = e;
        } else {
            c->ast[tail].next = (int16_t)e;
        }
        tail = e;
        if (at(c, TK_COMMA)) {
            adv(c);
            continue;
        }
        break;
    }
    *first = head;
    return 0;
}

static int swizzle_mask(const ShComp *c, int tok, int *mask, int *ncomp) {
    const Tok *t = &c->tok[tok];
    int i;
    int set = 0;
    int m = 0;
    if (t->kind != TK_IDENT || t->len < 1 || t->len > 4) {
        return -1;
    }
    for (i = 0; i < (int)t->len; ++i) {
        char ch = c->src[t->off + (uint32_t)i];
        int comp = -1;
        int which = 0;
        if (ch == 'x') {
            comp = 0;
            which = 1;
        } else if (ch == 'y') {
            comp = 1;
            which = 1;
        } else if (ch == 'z') {
            comp = 2;
            which = 1;
        } else if (ch == 'w') {
            comp = 3;
            which = 1;
        } else if (ch == 'r') {
            comp = 0;
            which = 2;
        } else if (ch == 'g') {
            comp = 1;
            which = 2;
        } else if (ch == 'b') {
            comp = 2;
            which = 2;
        } else if (ch == 'a') {
            comp = 3;
            which = 2;
        } else {
            return -1;
        }
        if (set == 0) {
            set = which;
        } else if (set != which) {
            return -2;
        }
        m |= comp << (i * 2);
    }
    *mask = m;
    *ncomp = (int)t->len;
    return 0;
}

static int parse_postfix(ShComp *c, int base) {
    for (;;) {
        if (at(c, TK_DOT)) {
            int dot = c->tp;
            int mask = 0;
            int nc = 0;
            int rc;
            int n;
            adv(c);
            if (!at(c, TK_IDENT)) {
                sh_err(c, c->tp, "expected a swizzle");
                return -1;
            }
            rc = swizzle_mask(c, c->tp, &mask, &nc);
            if (rc != 0) {
                sh_err(c, c->tp, rc == -2 ? "mixed swizzle sets" : "invalid swizzle");
                adv(c);
                return -1;
            }
            n = node_new(c, NK_SWZ, dot);
            if (n < 0) {
                return -1;
            }
            c->ast[n].a = (int16_t)base;
            c->ast[n].i = mask;
            c->ast[n].op = (uint8_t)nc;
            adv(c);
            base = n;
            continue;
        }
        if (at(c, TK_LBRACK)) {
            int br = c->tp;
            int idx;
            int n;
            adv(c);
            idx = parse_expr(c);
            if (idx < 0) {
                return -1;
            }
            if (!at(c, TK_RBRACK)) {
                sh_err(c, c->tp, "expected ']'");
                return -1;
            }
            adv(c);
            n = node_new(c, NK_INDEX, br);
            if (n < 0) {
                return -1;
            }
            c->ast[n].a = (int16_t)base;
            c->ast[n].b = (int16_t)idx;
            base = n;
            continue;
        }
        if (at(c, TK_INC) || at(c, TK_DEC)) {
            int n = node_new(c, NK_POST, c->tp);
            if (n < 0) {
                return -1;
            }
            c->ast[n].op = (uint8_t)peek(c);
            c->ast[n].a = (int16_t)base;
            adv(c);
            base = n;
            continue;
        }
        break;
    }
    return base;
}

static int parse_primary(ShComp *c) {
    int k;
    int n;
    if (enter(c) != 0) {
        return -1;
    }
    k = peek(c);
    if (k == TK_INT || k == TK_FLOAT || k == TK_TRUE || k == TK_FALSE) {
        n = node_new(c, NK_LIT, c->tp);
        if (n < 0) {
            leave(c);
            return -1;
        }
        if (k == TK_INT) {
            c->ast[n].op = 1;
            c->ast[n].i = (int32_t)c->tok[c->tp].ival;
            c->ast[n].f = c->tok[c->tp].fval;
        } else if (k == TK_FLOAT) {
            c->ast[n].op = 0;
            c->ast[n].f = c->tok[c->tp].fval;
        } else {
            c->ast[n].op = 2;
            c->ast[n].i = k == TK_TRUE ? 1 : 0;
            c->ast[n].f = c->ast[n].i ? 1.f : 0.f;
        }
        adv(c);
        leave(c);
        return n;
    }
    if (k == TK_IDENT) {
        n = node_new(c, NK_IDENT, c->tp);
        if (n < 0) {
            leave(c);
            return -1;
        }
        adv(c);
        if (at(c, TK_LPAREN)) {
            int call = node_new(c, NK_CALL, n >= 0 ? c->ast[n].tok : c->tp);
            int args = -1;
            if (call < 0) {
                leave(c);
                return -1;
            }
            adv(c);
            if (parse_args(c, &args) != 0) {
                leave(c);
                return -1;
            }
            if (!at(c, TK_RPAREN)) {
                sh_err(c, c->tp, "expected ')'");
                leave(c);
                return -1;
            }
            adv(c);
            c->ast[call].a = (int16_t)args;
            c->ast[call].tok = c->ast[n].tok;
            leave(c);
            return parse_postfix(c, call);
        }
        leave(c);
        return parse_postfix(c, n);
    }
    if (is_type_tok(k) && k != TK_VOID) {
        int ty = type_of_tok(k);
        int ctor;
        int args = -1;
        int tok = c->tp;
        adv(c);
        if (!at(c, TK_LPAREN)) {
            sh_err(c, c->tp, "expected '(' after type");
            leave(c);
            return -1;
        }
        adv(c);
        if (parse_args(c, &args) != 0) {
            leave(c);
            return -1;
        }
        if (!at(c, TK_RPAREN)) {
            sh_err(c, c->tp, "expected ')'");
            leave(c);
            return -1;
        }
        adv(c);
        ctor = node_new(c, NK_CTOR, tok);
        if (ctor < 0) {
            leave(c);
            return -1;
        }
        c->ast[ctor].ty = (uint8_t)ty;
        c->ast[ctor].a = (int16_t)args;
        leave(c);
        return parse_postfix(c, ctor);
    }
    if (k == TK_LPAREN) {
        int e;
        adv(c);
        e = parse_expr(c);
        if (e < 0) {
            leave(c);
            return -1;
        }
        if (!at(c, TK_RPAREN)) {
            sh_err(c, c->tp, "expected ')'");
            leave(c);
            return -1;
        }
        adv(c);
        leave(c);
        return parse_postfix(c, e);
    }
    sh_err(c, c->tp, "expected an expression");
    leave(c);
    return -1;
}

static int parse_unary(ShComp *c) {
    int k = peek(c);
    if (k == TK_PLUS || k == TK_MINUS || k == TK_NOT || k == TK_INC || k == TK_DEC) {
        int tok = c->tp;
        int e;
        int n;
        adv(c);
        e = parse_unary(c);
        if (e < 0) {
            return -1;
        }
        if (k == TK_PLUS) {
            return e;
        }
        n = node_new(c, (k == TK_INC || k == TK_DEC) ? NK_PRE : NK_UNARY, tok);
        if (n < 0) {
            return -1;
        }
        c->ast[n].op = (uint8_t)k;
        c->ast[n].a = (int16_t)e;
        return n;
    }
    return parse_primary(c);
}

static int parse_bin(ShComp *c, int min_prec) {
    static const struct {
        int k;
        int p;
        int r;
    } ops[] = {
        {TK_OR, 1, 0},  {TK_AND, 2, 0}, {TK_EQ, 3, 0},  {TK_NE, 3, 0},
        {TK_LT, 4, 0},  {TK_GT, 4, 0},  {TK_LE, 4, 0},  {TK_GE, 4, 0},
        {TK_PLUS, 5, 0},{TK_MINUS, 5, 0},{TK_STAR, 6, 0},{TK_SLASH, 6, 0},
        {TK_PERCENT, 6, 0},
    };
    int left;
    if (enter(c) != 0) {
        return -1;
    }
    left = parse_unary(c);
    if (left < 0) {
        leave(c);
        return -1;
    }
    for (;;) {
        int k = peek(c);
        int prec = 0;
        int i;
        int right;
        int n;
        int tok;
        for (i = 0; i < (int)(sizeof ops / sizeof ops[0]); ++i) {
            if (ops[i].k == k) {
                prec = ops[i].p;
                break;
            }
        }
        if (prec < min_prec || prec == 0) {
            break;
        }
        tok = c->tp;
        adv(c);
        right = parse_bin(c, prec + 1);
        if (right < 0) {
            leave(c);
            return -1;
        }
        n = node_new(c, NK_BINARY, tok);
        if (n < 0) {
            leave(c);
            return -1;
        }
        c->ast[n].op = (uint8_t)k;
        c->ast[n].a = (int16_t)left;
        c->ast[n].b = (int16_t)right;
        left = n;
    }
    leave(c);
    return left;
}

static int parse_expr(ShComp *c) {
    return parse_bin(c, 1);
}

static int parse_stmt(ShComp *c);

static int parse_block(ShComp *c) {
    int n;
    int head = -1;
    int tail = -1;
    int tok;
    if (!at(c, TK_LBRACE)) {
        sh_err(c, c->tp, "expected '{'");
        return -1;
    }
    tok = c->tp;
    adv(c);
    n = node_new(c, NK_BLOCK, tok);
    if (n < 0) {
        return -1;
    }
    while (!at(c, TK_RBRACE) && !at(c, TK_EOF)) {
        int s = parse_stmt(c);
        if (s < 0) {
            sync_stmt(c);
            if (c->nerr >= SH_ERR_MAX) {
                return -1;
            }
            continue;
        }
        if (head < 0) {
            head = s;
        } else {
            c->ast[tail].next = (int16_t)s;
        }
        tail = s;
    }
    if (!at(c, TK_RBRACE)) {
        sh_err(c, c->tp, "expected '}'");
        return -1;
    }
    adv(c);
    c->ast[n].a = (int16_t)head;
    return n;
}

static int parse_layout(ShComp *c, int *loc) {
    *loc = -1;
    if (!at(c, TK_LAYOUT)) {
        return 0;
    }
    adv(c);
    if (!at(c, TK_LPAREN)) {
        sh_err(c, c->tp, "expected '(' after layout");
        return -1;
    }
    adv(c);
    if (!at(c, TK_IDENT) || !sh_eqn(c->src + c->tok[c->tp].off, c->tok[c->tp].len, "location")) {
        sh_err(c, c->tp, "only layout(location = N) is supported");
        return -1;
    }
    adv(c);
    if (!at(c, TK_ASSIGN)) {
        sh_err(c, c->tp, "expected '=' in layout");
        return -1;
    }
    adv(c);
    if (!at(c, TK_INT)) {
        sh_err(c, c->tp, "expected an integer location");
        return -1;
    }
    *loc = (int)c->tok[c->tp].ival;
    adv(c);
    if (!at(c, TK_RPAREN)) {
        sh_err(c, c->tp, "expected ')' after layout");
        return -1;
    }
    adv(c);
    return 0;
}

static int parse_decl_tail(ShComp *c, int ty, int quals, int loc, int name_tok, int *out) {
    int n = node_new(c, NK_DECL, name_tok);
    if (n < 0) {
        return -1;
    }
    c->ast[n].ty = (uint8_t)ty;
    c->ast[n].quals = (uint8_t)quals;
    c->ast[n].i = loc;
    if (at(c, TK_ASSIGN)) {
        int init;
        adv(c);
        init = parse_expr(c);
        if (init < 0) {
            return -1;
        }
        c->ast[n].a = (int16_t)init;
    }
    if (!at(c, TK_SEMI)) {
        sh_err(c, c->tp, "expected ';'");
        return -1;
    }
    adv(c);
    *out = n;
    return 0;
}

static int parse_param(ShComp *c) {
    int ty;
    int n;
    if (!is_type_tok(peek(c))) {
        sh_err(c, c->tp, "expected a parameter type");
        return -1;
    }
    ty = type_of_tok(peek(c));
    adv(c);
    if (!at(c, TK_IDENT)) {
        sh_err(c, c->tp, "expected a parameter name");
        return -1;
    }
    n = node_new(c, NK_DECL, c->tp);
    if (n < 0) {
        return -1;
    }
    c->ast[n].ty = (uint8_t)ty;
    adv(c);
    return n;
}

static int parse_global_or_func(ShComp *c) {
    int quals = 0;
    int loc = -1;
    int ty;
    int name;
    int k;
    if (parse_layout(c, &loc) != 0) {
        return -1;
    }
    if (at(c, TK_SMOOTH)) {
        quals |= Q_SMOOTH;
        adv(c);
    }
    k = peek(c);
    if (k == TK_IN || k == TK_OUT || k == TK_UNIFORM || k == TK_CONST) {
        if (k == TK_IN) {
            quals |= Q_IN;
        } else if (k == TK_OUT) {
            quals |= Q_OUT;
        } else if (k == TK_UNIFORM) {
            quals |= Q_UNIFORM;
        } else {
            quals |= Q_CONST;
        }
        adv(c);
        if (at(c, TK_CONST) && (quals & Q_CONST) == 0) {
            quals |= Q_CONST;
            adv(c);
        }
    }
    if (at(c, TK_BAD)) {
        sh_err(c, c->tp, "unsupported qualifier");
        adv(c);
        sync_stmt(c);
        return node_new(c, NK_EMPTY, c->tp);
    }
    if (!is_type_tok(peek(c))) {
        sh_err(c, c->tp, "expected a type");
        return -1;
    }
    ty = type_of_tok(peek(c));
    adv(c);
    if (!at(c, TK_IDENT)) {
        sh_err(c, c->tp, "expected a name");
        return -1;
    }
    name = c->tp;
    adv(c);
    if (at(c, TK_LPAREN)) {
        int fn = node_new(c, NK_FUNC, name);
        int head = -1;
        int tail = -1;
        int body;
        if (fn < 0) {
            return -1;
        }
        c->ast[fn].ty = (uint8_t)ty;
        adv(c);
        if (!at(c, TK_RPAREN)) {
            for (;;) {
                int p = parse_param(c);
                if (p < 0) {
                    return -1;
                }
                if (head < 0) {
                    head = p;
                } else {
                    c->ast[tail].next = (int16_t)p;
                }
                tail = p;
                if (at(c, TK_COMMA)) {
                    adv(c);
                    continue;
                }
                break;
            }
        }
        if (!at(c, TK_RPAREN)) {
            sh_err(c, c->tp, "expected ')'");
            return -1;
        }
        adv(c);
        c->ast[fn].b = (int16_t)head;
        if (!at(c, TK_LBRACE)) {
            sh_err(c, c->tp, "expected a function body");
            return -1;
        }
        body = parse_block(c);
        if (body < 0) {
            return -1;
        }
        c->ast[fn].a = (int16_t)body;
        return fn;
    }
    {
        int d = -1;
        if (parse_decl_tail(c, ty, quals, loc, name, &d) != 0) {
            return -1;
        }
        return d;
    }
}

static int parse_stmt(ShComp *c) {
    int k = peek(c);
    if (k == TK_SEMI) {
        int n = node_new(c, NK_EMPTY, c->tp);
        adv(c);
        return n;
    }
    if (k == TK_LBRACE) {
        return parse_block(c);
    }
    if (k == TK_DISCARD) {
        int n = node_new(c, NK_DISCARD, c->tp);
        adv(c);
        if (!at(c, TK_SEMI)) {
            sh_err(c, c->tp, "expected ';'");
            return -1;
        }
        adv(c);
        return n;
    }
    if (k == TK_RETURN) {
        int tok = c->tp;
        int n;
        adv(c);
        n = node_new(c, NK_RETURN, tok);
        if (n < 0) {
            return -1;
        }
        if (!at(c, TK_SEMI)) {
            int e = parse_expr(c);
            if (e < 0) {
                return -1;
            }
            c->ast[n].a = (int16_t)e;
        }
        if (!at(c, TK_SEMI)) {
            sh_err(c, c->tp, "expected ';'");
            return -1;
        }
        adv(c);
        return n;
    }
    if (k == TK_IF) {
        int tok = c->tp;
        int cond;
        int th;
        int el = -1;
        int n;
        adv(c);
        if (!at(c, TK_LPAREN)) {
            sh_err(c, c->tp, "expected '(' after if");
            return -1;
        }
        adv(c);
        cond = parse_expr(c);
        if (cond < 0) {
            return -1;
        }
        if (!at(c, TK_RPAREN)) {
            sh_err(c, c->tp, "expected ')'");
            return -1;
        }
        adv(c);
        th = parse_stmt(c);
        if (th < 0) {
            return -1;
        }
        if (at(c, TK_ELSE)) {
            adv(c);
            el = parse_stmt(c);
            if (el < 0) {
                return -1;
            }
        }
        n = node_new(c, NK_IF, tok);
        if (n < 0) {
            return -1;
        }
        c->ast[n].a = (int16_t)cond;
        c->ast[n].b = (int16_t)th;
        c->ast[n].c = (int16_t)el;
        return n;
    }
    if (k == TK_FOR) {
        int tok = c->tp;
        int init = -1;
        int cond = -1;
        int step = -1;
        int body;
        int n;
        adv(c);
        if (!at(c, TK_LPAREN)) {
            sh_err(c, c->tp, "expected '(' after for");
            return -1;
        }
        adv(c);
        if (is_type_tok(peek(c))) {
            int ty = type_of_tok(peek(c));
            int name;
            adv(c);
            if (!at(c, TK_IDENT)) {
                sh_err(c, c->tp, "expected a loop variable");
                return -1;
            }
            name = c->tp;
            adv(c);
            if (parse_decl_tail(c, ty, 0, -1, name, &init) != 0) {
                return -1;
            }
        } else if (!at(c, TK_SEMI)) {
            init = parse_expr(c);
            if (init < 0) {
                return -1;
            }
            if (!at(c, TK_SEMI)) {
                sh_err(c, c->tp, "expected ';'");
                return -1;
            }
            adv(c);
        } else {
            adv(c);
        }
        if (!at(c, TK_SEMI)) {
            cond = parse_expr(c);
            if (cond < 0) {
                return -1;
            }
        }
        if (!at(c, TK_SEMI)) {
            sh_err(c, c->tp, "expected ';'");
            return -1;
        }
        adv(c);
        if (!at(c, TK_RPAREN)) {
            step = parse_expr(c);
            if (step < 0) {
                return -1;
            }
        }
        if (!at(c, TK_RPAREN)) {
            sh_err(c, c->tp, "expected ')'");
            return -1;
        }
        adv(c);
        body = parse_stmt(c);
        if (body < 0) {
            return -1;
        }
        n = node_new(c, NK_FOR, tok);
        if (n < 0) {
            return -1;
        }
        c->ast[n].a = (int16_t)init;
        c->ast[n].b = (int16_t)cond;
        c->ast[n].c = (int16_t)step;
        c->ast[n].d = (int16_t)body;
        return n;
    }
    if (k == TK_LAYOUT || k == TK_IN || k == TK_OUT || k == TK_UNIFORM || k == TK_CONST ||
        k == TK_SMOOTH || is_type_tok(k)) {
        return parse_global_or_func(c);
    }
    {
        int e = parse_expr(c);
        int n;
        if (e < 0) {
            return -1;
        }
        if (at(c, TK_ASSIGN)) {
            int rhs;
            int tok = c->tp;
            adv(c);
            rhs = parse_expr(c);
            if (rhs < 0) {
                return -1;
            }
            n = node_new(c, NK_ASSIGN, tok);
            if (n < 0) {
                return -1;
            }
            c->ast[n].a = (int16_t)e;
            c->ast[n].b = (int16_t)rhs;
        } else {
            n = node_new(c, NK_EXPR, c->ast[e].tok);
            if (n < 0) {
                return -1;
            }
            c->ast[n].a = (int16_t)e;
        }
        if (!at(c, TK_SEMI)) {
            sh_err(c, c->tp, "expected ';'");
            return -1;
        }
        adv(c);
        return n;
    }
}

int sh_parse(ShComp *c) {
    int head = -1;
    int tail = -1;
    if (!c) {
        return -1;
    }
    c->root = -1;
    while (!at(c, TK_EOF)) {
        int g;
        if (c->nerr >= SH_ERR_MAX) {
            break;
        }
        g = parse_global_or_func(c);
        if (g < 0) {
            sync_stmt(c);
            if (c->nerr >= SH_ERR_MAX) {
                break;
            }
            continue;
        }
        if (head < 0) {
            head = g;
        } else {
            c->ast[tail].next = (int16_t)g;
        }
        tail = g;
    }
    c->root = head;
    return c->nerr ? -1 : 0;
}
