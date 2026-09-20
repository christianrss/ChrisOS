#include "chrisc.h"
#include "../clvm/clvm.h"
#include "../clvm/clvm_vm.h"

#define TOK_MAX 8192
#define NODE_MAX 4096
#define ARG_MAX 4096
#define SYM_MAX 256
#define NAME_MAX 24
#define FUNC_MAX 32
#define STRUCT_MAX 16
#define FIELD_MAX 8
#define CALL_PATCH_MAX 256

typedef enum TokenKind {
    T_EOF = 0, T_ID, T_NUM, T_FNUM, T_VOID, T_INT, T_FLOAT, T_STRUCT,
    T_IF, T_ELSE, T_WHILE, T_FOR, T_RETURN,
    T_LP, T_RP, T_LB, T_RB, T_LBRACK, T_RBRACK, T_SEMI, T_COMMA, T_ASSIGN,
    T_PLUS, T_MINUS, T_DOT,
    T_STAR, T_SLASH, T_PERCENT, T_EQ, T_NE, T_LT, T_LE, T_GT, T_GE
} TokenKind;

typedef struct Token {
    TokenKind kind;
    int32_t value;
    int line, column;
    char name[NAME_MAX];
} Token;

typedef enum NodeKind {
    N_BLOCK, N_DECL, N_ASSIGN, N_INDEX_ASSIGN, N_IF, N_WHILE, N_FOR, N_RETURN,
    N_EXPR, N_FIELD, N_FIELD_ASSIGN,
    N_INT, N_FLOAT, N_VAR, N_INDEX, N_CALL, N_NEG, N_ADD, N_SUB, N_MUL, N_DIV,
    N_MOD,
    N_EQ, N_NE, N_LT, N_LE, N_GT, N_GE
} NodeKind;

typedef struct Node {
    NodeKind kind;
    int left, right, third, next;
    int32_t value;
    int line, column;
    uint8_t is_float;
    char name[NAME_MAX];
} Node;

typedef struct Symbol {
    char name[NAME_MAX];
    uint16_t address;
    uint8_t is_float;
    int8_t struct_id;
    uint16_t stride;
} Symbol;

typedef struct StructDef {
    char name[NAME_MAX];
    int nfields;
    char fname[FIELD_MAX][NAME_MAX];
    uint8_t is_float[FIELD_MAX];
    uint16_t size;
} StructDef;

typedef struct FuncDef {
    char name[NAME_MAX];
    uint8_t argc;
    uint8_t ret;
    uint8_t arg_float[8];
    uint16_t arg_addr[8];
    int body;
    int entry;
    uint8_t is_main;
} FuncDef;

typedef struct CallPatch {
    int at;
    int fn;
} CallPatch;

typedef struct Builtin {
    const char *name;
    uint8_t id, argc, returns;
} Builtin;

typedef struct Compiler {
    Token tokens[TOK_MAX];
    int ntok, pos;
    Node nodes[NODE_MAX];
    int nnode;
    int args[ARG_MAX];
    int nargs;
    Symbol syms[SYM_MAX];
    int nsyms;
    StructDef structs[STRUCT_MAX];
    int nstructs;
    FuncDef funcs[FUNC_MAX];
    int nfuncs;
    CallPatch patches[CALL_PATCH_MAX];
    int npatches;
    int cur_fn;
    int scope_base;
    uint16_t mem_next;
    uint8_t *out;
    size_t cap, pc;
    ChrisResult *result;
} Compiler;

static Compiler g_chrisc;

static const Builtin builtins[] = {
    {"pixel", 1, 3, 0}, {"rect", 2, 5, 0}, {"line", 3, 5, 0},
    {"sprite", 4, 6, 0}, {"tilemap", 5, 7, 0}, {"clear", 6, 1, 0},
    {"key", 10, 1, 1}, {"ticks", 11, 0, 1}, {"wait", 12, 1, 0},
    {"tone", 13, 2, 0}, {"tri", 20, 10, 0}, {"mesh", 21, 5, 0},
    {"transform", 22, 3, 0}, {"meshf", 23, 8, 0},
    {"sin", 31, 1, 1}, {"cos", 32, 1, 1},
    {"cam", 33, 5, 0}, {"light", 34, 6, 0}, {"tex", 35, 1, 0},
    {"voxel", 36, 4, 0}, {"voxel_get", 37, 3, 1}, {"world", 38, 0, 0},
    {"viewport", 39, 2, 0}, {"screen_w", 40, 0, 1}, {"screen_h", 41, 0, 1},
    {"fps", 30, 0, 1}
};

static int alpha(int c) {
    return c == '_' || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

static int digit(int c) {
    return c >= '0' && c <= '9';
}

static int alnum(int c) {
    return alpha(c) || digit(c);
}

static void text(char *d, size_t n, const char *s) {
    size_t i = 0;
    if (!n) {
        return;
    }
    while (s[i] && i + 1 < n) {
        d[i] = s[i];
        ++i;
    }
    d[i] = 0;
}

static int same(const char *a, const char *b) {
    size_t i = 0;
    while (a[i] && a[i] == b[i]) {
        ++i;
    }
    return a[i] == 0 && b[i] == 0;
}

static int fail(Compiler *c, int line, int col, const char *m) {
    c->result->diag.line = line;
    c->result->diag.column = col;
    text(c->result->diag.message, sizeof(c->result->diag.message), m);
    return 0;
}

static TokenKind keyword(const char *s) {
    if (same(s, "void")) {
        return T_VOID;
    }
    if (same(s, "int")) {
        return T_INT;
    }
    if (same(s, "float")) {
        return T_FLOAT;
    }
    if (same(s, "struct")) {
        return T_STRUCT;
    }
    if (same(s, "if")) {
        return T_IF;
    }
    if (same(s, "else")) {
        return T_ELSE;
    }
    if (same(s, "while")) {
        return T_WHILE;
    }
    if (same(s, "for")) {
        return T_FOR;
    }
    if (same(s, "return")) {
        return T_RETURN;
    }
    return T_ID;
}

static int token(Compiler *c, TokenKind k, int line, int col) {
    Token *t;
    if (c->ntok == TOK_MAX) {
        return fail(c, line, col, "too many tokens");
    }
    t = &c->tokens[c->ntok++];
    t->kind = k;
    t->value = 0;
    t->line = line;
    t->column = col;
    t->name[0] = 0;
    return 1;
}

static int lex(Compiler *c, const char *s, size_t n) {
    size_t i = 0;
    int line = 1;
    int col = 1;

    if (n == 0 || n > 32768u) {
        return fail(c, 1, 1, "source size outside limit");
    }
    while (i < n) {
        int ch = (unsigned char)s[i];
        int start = col;

        if (ch == ' ' || ch == '\t' || ch == '\r') {
            ++i;
            ++col;
            continue;
        }
        if (ch == '\n') {
            ++i;
            ++line;
            col = 1;
            continue;
        }
        if (ch == '/' && i + 1 < n && s[i + 1] == '/') {
            i += 2;
            col += 2;
            while (i < n && s[i] != '\n') {
                ++i;
                ++col;
            }
            continue;
        }
        if (alpha(ch)) {
            char name[NAME_MAX];
            size_t k = 0;
            while (i < n && alnum((unsigned char)s[i])) {
                if (k + 1 >= NAME_MAX) {
                    return fail(c, line, start, "identifier too long");
                }
                name[k++] = s[i++];
                ++col;
            }
            name[k] = 0;
            if (!token(c, keyword(name), line, start)) {
                return 0;
            }
            text(c->tokens[c->ntok - 1].name, NAME_MAX, name);
            continue;
        }
        if (ch == '.' && i + 1 < n && digit((unsigned char)s[i + 1])) {
            union {
                float f;
                uint32_t u;
            } bits;
            float fv = 0.0f;
            float frac = 0.1f;

            ++i;
            ++col;
            while (i < n && digit((unsigned char)s[i])) {
                fv += (float)(s[i] - '0') * frac;
                frac *= 0.1f;
                ++i;
                ++col;
            }
            bits.f = fv;
            if (!token(c, T_FNUM, line, start)) {
                return 0;
            }
            c->tokens[c->ntok - 1].value = (int32_t)bits.u;
            continue;
        }
        if (digit(ch)) {
            uint32_t v = 0;
            int base = 10;
            int d;
            int any = 0;

            if (ch == '0' && i + 1 < n && (s[i + 1] == 'x' || s[i + 1] == 'X')) {
                base = 16;
                i += 2;
                col += 2;
            }
            while (i < n) {
                ch = (unsigned char)s[i];
                if (digit(ch)) {
                    d = ch - '0';
                } else if (ch >= 'a' && ch <= 'f') {
                    d = ch - 'a' + 10;
                } else if (ch >= 'A' && ch <= 'F') {
                    d = ch - 'A' + 10;
                } else {
                    d = -1;
                }
                if (d < 0 || d >= base) {
                    break;
                }
                if (v > (0xffffffffu - (uint32_t)d) / (uint32_t)base) {
                    return fail(c, line, start, "integer literal overflow");
                }
                v = v * (uint32_t)base + (uint32_t)d;
                ++i;
                ++col;
                any = 1;
            }
            if (!any) {
                return fail(c, line, start, "bad integer literal");
            }
            if (base == 10 && i < n && s[i] == '.') {
                union {
                    float f;
                    uint32_t u;
                } bits;
                float fv = (float)v;
                float frac = 0.1f;

                ++i;
                ++col;
                while (i < n && digit((unsigned char)s[i])) {
                    fv += (float)(s[i] - '0') * frac;
                    frac *= 0.1f;
                    ++i;
                    ++col;
                }
                bits.f = fv;
                if (!token(c, T_FNUM, line, start)) {
                    return 0;
                }
                c->tokens[c->ntok - 1].value = (int32_t)bits.u;
                continue;
            }
            if (!token(c, T_NUM, line, start)) {
                return 0;
            }
            c->tokens[c->ntok - 1].value = (int32_t)v;
            continue;
        }
#define ONE(character, kind) \
    case character: \
        if (!token(c, kind, line, start)) { \
            return 0; \
        } \
        ++i; \
        ++col; \
        break
        switch (ch) {
        ONE('(', T_LP);
        ONE(')', T_RP);
        ONE('{', T_LB);
        ONE('}', T_RB);
        ONE('[', T_LBRACK);
        ONE(']', T_RBRACK);
        ONE(';', T_SEMI);
        ONE(',', T_COMMA);
        ONE('+', T_PLUS);
        ONE('-', T_MINUS);
        ONE('.', T_DOT);
        ONE('*', T_STAR);
        ONE('/', T_SLASH);
        ONE('%', T_PERCENT);
        case '=':
            if (i + 1 < n && s[i + 1] == '=') {
                if (!token(c, T_EQ, line, start)) {
                    return 0;
                }
                i += 2;
                col += 2;
            } else {
                if (!token(c, T_ASSIGN, line, start)) {
                    return 0;
                }
                ++i;
                ++col;
            }
            break;
        case '!':
            if (i + 1 < n && s[i + 1] == '=') {
                if (!token(c, T_NE, line, start)) {
                    return 0;
                }
                i += 2;
                col += 2;
            } else {
                return fail(c, line, start, "expected !=");
            }
            break;
        case '<':
            if (i + 1 < n && s[i + 1] == '=') {
                if (!token(c, T_LE, line, start)) {
                    return 0;
                }
                i += 2;
                col += 2;
            } else {
                if (!token(c, T_LT, line, start)) {
                    return 0;
                }
                ++i;
                ++col;
            }
            break;
        case '>':
            if (i + 1 < n && s[i + 1] == '=') {
                if (!token(c, T_GE, line, start)) {
                    return 0;
                }
                i += 2;
                col += 2;
            } else {
                if (!token(c, T_GT, line, start)) {
                    return 0;
                }
                ++i;
                ++col;
            }
            break;
        default:
            return fail(c, line, start, "invalid character");
        }
#undef ONE
    }
    return token(c, T_EOF, line, col);
}

static Token *cur(Compiler *c) {
    return &c->tokens[c->pos];
}

static int take(Compiler *c, TokenKind k) {
    if (cur(c)->kind == k) {
        ++c->pos;
        return 1;
    }
    return 0;
}

static int expect(Compiler *c, TokenKind k, const char *m) {
    if (take(c, k)) {
        return 1;
    }
    return fail(c, cur(c)->line, cur(c)->column, m);
}

static int node(Compiler *c, NodeKind k, Token *t) {
    Node *n;
    int id;

    if (c->nnode == NODE_MAX) {
        fail(c, t->line, t->column, "AST full");
        return -1;
    }
    id = c->nnode++;
    n = &c->nodes[id];
    n->kind = k;
    n->left = n->right = n->third = n->next = -1;
    n->value = 0;
    n->line = t->line;
    n->column = t->column;
    n->is_float = 0;
    n->name[0] = 0;
    return id;
}

static int sym_find(Compiler *c, const char *name) {
    int i;
    for (i = c->scope_base; i < c->nsyms; ++i) {
        if (same(c->syms[i].name, name)) {
            return i;
        }
    }
    return -1;
}

static int sym_add(Compiler *c, Token *t, uint8_t is_float) {
    int i = sym_find(c, t->name);
    if (i >= 0) {
        fail(c, t->line, t->column, "duplicate variable");
        return -1;
    }
    if (c->nsyms == SYM_MAX) {
        fail(c, t->line, t->column, "symbol table full");
        return -1;
    }
    i = c->nsyms++;
    text(c->syms[i].name, NAME_MAX, t->name);
    c->syms[i].address = c->mem_next;
    c->syms[i].is_float = is_float;
    c->syms[i].struct_id = -1;
    c->syms[i].stride = 4;
    c->mem_next += 4;
    return i;
}

static int sym_add_array(Compiler *c, Token *t, int32_t count, uint8_t is_float) {
    int i;
    uint16_t need;

    if (count < 1 || count > 4096) {
        fail(c, t->line, t->column, "array size out of range");
        return -1;
    }
    i = sym_find(c, t->name);
    if (i >= 0) {
        fail(c, t->line, t->column, "duplicate variable");
        return -1;
    }
    if (c->nsyms == SYM_MAX) {
        fail(c, t->line, t->column, "symbol table full");
        return -1;
    }
    need = (uint16_t)(count * 4);
    if (c->mem_next + need > CLVM_MEMORY_SIZE) {
        fail(c, t->line, t->column, "array exceeds CLVM memory");
        return -1;
    }
    i = c->nsyms++;
    text(c->syms[i].name, NAME_MAX, t->name);
    c->syms[i].address = c->mem_next;
    c->syms[i].is_float = is_float;
    c->syms[i].struct_id = -1;
    c->syms[i].stride = 4;
    c->mem_next += need;
    return i;
}

static int struct_find(Compiler *c, const char *name) {
    int i;
    for (i = 0; i < c->nstructs; ++i) {
        if (same(c->structs[i].name, name)) {
            return i;
        }
    }
    return -1;
}

static int func_find(Compiler *c, const char *name) {
    int i;
    for (i = 0; i < c->nfuncs; ++i) {
        if (same(c->funcs[i].name, name)) {
            return i;
        }
    }
    return -1;
}

static int field_find(StructDef *s, const char *name) {
    int i;
    for (i = 0; i < s->nfields; ++i) {
        if (same(s->fname[i], name)) {
            return i;
        }
    }
    return -1;
}

static int parse_struct_def(Compiler *c) {
    Token *name;
    StructDef *s;
    if (!expect(c, T_ID, "expected struct name")) {
        return 0;
    }
    name = &c->tokens[c->pos - 1];
    if (c->nstructs == STRUCT_MAX) {
        return fail(c, name->line, name->column, "too many structs");
    }
    if (struct_find(c, name->name) >= 0) {
        return fail(c, name->line, name->column, "duplicate struct");
    }
    s = &c->structs[c->nstructs++];
    text(s->name, NAME_MAX, name->name);
    s->nfields = 0;
    s->size = 0;
    if (!expect(c, T_LB, "expected {")) {
        return 0;
    }
    while (!take(c, T_RB)) {
        uint8_t isf = 0;
        Token *fn;
        if (take(c, T_FLOAT)) {
            isf = 1;
        } else if (!take(c, T_INT)) {
            return fail(c, cur(c)->line, cur(c)->column, "expected field type");
        }
        fn = cur(c);
        if (!expect(c, T_ID, "expected field name")) {
            return 0;
        }
        if (s->nfields == FIELD_MAX) {
            return fail(c, fn->line, fn->column, "too many fields");
        }
        text(s->fname[s->nfields], NAME_MAX, fn->name);
        s->is_float[s->nfields] = isf;
        s->nfields++;
        s->size += 4;
        if (!expect(c, T_SEMI, "expected ; after field")) {
            return 0;
        }
    }
    return expect(c, T_SEMI, "expected ; after struct");
}

static const Builtin *builtin(const char *name) {
    size_t i;
    for (i = 0; i < sizeof(builtins) / sizeof(builtins[0]); ++i) {
        if (same(name, builtins[i].name)) {
            return &builtins[i];
        }
    }
    return 0;
}

static int expression(Compiler *c);

static int primary(Compiler *c) {
    Token *t = cur(c);
    int id;
    int sym;
    int first;
    int count;
    int arg;

    if (take(c, T_NUM)) {
        id = node(c, N_INT, t);
        if (id >= 0) {
            c->nodes[id].value = t->value;
        }
        return id;
    }
    if (take(c, T_FNUM)) {
        id = node(c, N_FLOAT, t);
        if (id >= 0) {
            c->nodes[id].value = t->value;
            c->nodes[id].is_float = 1;
        }
        return id;
    }
    if (take(c, T_LP)) {
        id = expression(c);
        if (id < 0 || !expect(c, T_RP, "expected )")) {
            return -1;
        }
        return id;
    }
    if (t->kind != T_ID) {
        fail(c, t->line, t->column, "expected expression");
        return -1;
    }
    ++c->pos;
    if (take(c, T_LP)) {
        id = node(c, N_CALL, t);
        if (id < 0) {
            return -1;
        }
        text(c->nodes[id].name, NAME_MAX, t->name);
        first = c->nargs;
        count = 0;
        if (!take(c, T_RP)) {
            do {
                if (c->nargs == ARG_MAX) {
                    fail(c, t->line, t->column, "argument table full");
                    return -1;
                }
                arg = expression(c);
                if (arg < 0) {
                    return -1;
                }
                c->args[c->nargs++] = arg;
                ++count;
            } while (take(c, T_COMMA));
            if (!expect(c, T_RP, "expected ) after arguments")) {
                return -1;
            }
        }
        c->nodes[id].left = first;
        c->nodes[id].value = count;
        return id;
    }
    sym = sym_find(c, t->name);
    if (sym < 0) {
        fail(c, t->line, t->column, "unknown variable");
        return -1;
    }
    if (take(c, T_LBRACK)) {
        int idx = expression(c);
        if (idx < 0 || !expect(c, T_RBRACK, "expected ]")) {
            return -1;
        }
        id = node(c, N_INDEX, t);
        if (id >= 0) {
            c->nodes[id].value = sym;
            c->nodes[id].left = idx;
            c->nodes[id].is_float = c->syms[sym].is_float;
        }
        if (take(c, T_DOT)) {
            Token *fld = cur(c);
            int fi;
            int fid;
            if (c->syms[sym].struct_id < 0) {
                fail(c, fld->line, fld->column, "not a struct");
                return -1;
            }
            if (!expect(c, T_ID, "expected field")) {
                return -1;
            }
            fi = field_find(&c->structs[c->syms[sym].struct_id], fld->name);
            if (fi < 0) {
                fail(c, fld->line, fld->column, "unknown field");
                return -1;
            }
            fid = node(c, N_FIELD, fld);
            if (fid >= 0) {
                c->nodes[fid].value = sym;
                c->nodes[fid].left = fi;
                c->nodes[fid].right = id;
                c->nodes[fid].is_float = c->structs[c->syms[sym].struct_id].is_float[fi];
            }
            return fid;
        }
        return id;
    }
    id = node(c, N_VAR, t);
    if (id >= 0) {
        c->nodes[id].value = sym;
        c->nodes[id].is_float = c->syms[sym].is_float;
    }
    if (take(c, T_DOT)) {
        Token *fld = cur(c);
        int fi;
        int fid;
        if (c->syms[sym].struct_id < 0) {
            fail(c, t->line, t->column, "not a struct");
            return -1;
        }
        if (!expect(c, T_ID, "expected field")) {
            return -1;
        }
        fi = field_find(&c->structs[c->syms[sym].struct_id], fld->name);
        if (fi < 0) {
            fail(c, fld->line, fld->column, "unknown field");
            return -1;
        }
        fid = node(c, N_FIELD, fld);
        if (fid >= 0) {
            c->nodes[fid].value = sym;
            c->nodes[fid].left = fi;
            c->nodes[fid].right = -1;
            c->nodes[fid].is_float = c->structs[c->syms[sym].struct_id].is_float[fi];
        }
        return fid;
    }
    return id;
}

static int unary(Compiler *c) {
    Token *t = cur(c);
    int id;
    int v;

    if (take(c, T_MINUS)) {
        v = unary(c);
        if (v < 0) {
            return -1;
        }
        id = node(c, N_NEG, t);
        if (id >= 0) {
            c->nodes[id].left = v;
            c->nodes[id].is_float = c->nodes[v].is_float;
        }
        return id;
    }
    return primary(c);
}

static int binary(Compiler *c, int (*sub)(Compiler *), const TokenKind *kinds,
                  const NodeKind *nodes, int count) {
    int left = sub(c);
    int i;
    int right;
    int id;
    Token *t;

    if (left < 0) {
        return -1;
    }
    for (;;) {
        for (i = 0; i < count && cur(c)->kind != kinds[i]; ++i) {
        }
        if (i == count) {
            return left;
        }
        t = cur(c);
        ++c->pos;
        right = sub(c);
        if (right < 0) {
            return -1;
        }
        id = node(c, nodes[i], t);
        if (id < 0) {
            return -1;
        }
        c->nodes[id].left = left;
        c->nodes[id].right = right;
        c->nodes[id].is_float =
            c->nodes[left].is_float || c->nodes[right].is_float;
        left = id;
    }
}

static int product(Compiler *c) {
    static const TokenKind k[] = {T_STAR, T_SLASH, T_PERCENT};
    static const NodeKind n[] = {N_MUL, N_DIV, N_MOD};
    return binary(c, unary, k, n, 3);
}

static int sum(Compiler *c) {
    static const TokenKind k[] = {T_PLUS, T_MINUS};
    static const NodeKind n[] = {N_ADD, N_SUB};
    return binary(c, product, k, n, 2);
}

static int relation(Compiler *c) {
    static const TokenKind k[] = {T_LT, T_LE, T_GT, T_GE};
    static const NodeKind n[] = {N_LT, N_LE, N_GT, N_GE};
    return binary(c, sum, k, n, 4);
}

static int equality(Compiler *c) {
    static const TokenKind k[] = {T_EQ, T_NE};
    static const NodeKind n[] = {N_EQ, N_NE};
    return binary(c, relation, k, n, 2);
}

static int expression(Compiler *c) {
    return equality(c);
}

static int block(Compiler *c);

static int for_step(Compiler *c) {
    Token *name;
    int id;
    int sym;
    int v;
    int idx;

    if (cur(c)->kind != T_ID) {
        return fail(c, cur(c)->line, cur(c)->column, "expected assignment in for");
    }
    name = cur(c);
    ++c->pos;
    sym = sym_find(c, name->name);
    if (sym < 0) {
        return fail(c, name->line, name->column, "unknown variable");
    }
    if (take(c, T_LBRACK)) {
        idx = expression(c);
        if (idx < 0 || !expect(c, T_RBRACK, "expected ]") ||
            !expect(c, T_ASSIGN, "expected =")) {
            return -1;
        }
        id = node(c, N_INDEX_ASSIGN, name);
        if (id < 0) {
            return -1;
        }
        v = expression(c);
        if (v < 0) {
            return -1;
        }
        c->nodes[id].value = sym;
        c->nodes[id].left = v;
        c->nodes[id].right = idx;
        return id;
    }
    if (!expect(c, T_ASSIGN, "expected =")) {
        return -1;
    }
    id = node(c, N_ASSIGN, name);
    if (id < 0) {
        return -1;
    }
    v = expression(c);
    if (v < 0) {
        return -1;
    }
    c->nodes[id].value = sym;
    c->nodes[id].left = v;
    return id;
}

static int for_init(Compiler *c) {
    Token *t = cur(c);
    Token *name;
    int id;
    int sym;
    int v;
    uint8_t is_float = 0;

    if (take(c, T_INT)) {
        is_float = 0;
    } else if (take(c, T_FLOAT)) {
        is_float = 1;
    } else if (t->kind == T_ID) {
        name = t;
        ++c->pos;
        sym = sym_find(c, name->name);
        if (sym < 0) {
            return fail(c, name->line, name->column, "unknown variable");
        }
        if (!expect(c, T_ASSIGN, "expected =")) {
            return -1;
        }
        id = node(c, N_ASSIGN, name);
        if (id < 0) {
            return -1;
        }
        v = expression(c);
        if (v < 0) {
            return -1;
        }
        c->nodes[id].value = sym;
        c->nodes[id].left = v;
        return id;
    } else {
        return fail(c, t->line, t->column, "expected for init");
    }
    name = cur(c);
    if (!expect(c, T_ID, "expected variable name")) {
        return -1;
    }
    if (take(c, T_LBRACK)) {
        Token *sz = cur(c);
        if (!expect(c, T_NUM, "expected array size")) {
            return -1;
        }
        if (!expect(c, T_RBRACK, "expected ]")) {
            return -1;
        }
        sym = sym_add_array(c, name, sz->value, is_float);
    } else {
        sym = sym_add(c, name, is_float);
    }
    if (sym < 0) {
        return -1;
    }
    id = node(c, N_DECL, t);
    if (id < 0) {
        return -1;
    }
    c->nodes[id].value = sym;
    if (take(c, T_ASSIGN)) {
        v = expression(c);
        if (v < 0) {
            return -1;
        }
        c->nodes[id].left = v;
    }
    return id;
}

static int statement(Compiler *c) {
    Token *t = cur(c);
    Token *name;
    int id;
    int sym;
    int v;
    int b;
    int init;
    int step;

    if (take(c, T_INT)) {
        name = cur(c);
        if (!expect(c, T_ID, "expected variable name")) {
            return -1;
        }
        if (take(c, T_LBRACK)) {
            Token *sz = cur(c);
            if (!expect(c, T_NUM, "expected array size")) {
                return -1;
            }
            if (!expect(c, T_RBRACK, "expected ]")) {
                return -1;
            }
            sym = sym_add_array(c, name, sz->value, 0);
        } else {
            sym = sym_add(c, name, 0);
        }
        if (sym < 0) {
            return -1;
        }
        id = node(c, N_DECL, t);
        if (id < 0) {
            return -1;
        }
        c->nodes[id].value = sym;
        if (take(c, T_ASSIGN)) {
            v = expression(c);
            if (v < 0) {
                return -1;
            }
            c->nodes[id].left = v;
        }
        if (!expect(c, T_SEMI, "expected ; after declaration")) {
            return -1;
        }
        return id;
    }
    if (take(c, T_FLOAT)) {
        name = cur(c);
        if (!expect(c, T_ID, "expected variable name")) {
            return -1;
        }
        if (take(c, T_LBRACK)) {
            Token *sz = cur(c);
            if (!expect(c, T_NUM, "expected array size")) {
                return -1;
            }
            if (!expect(c, T_RBRACK, "expected ]")) {
                return -1;
            }
            sym = sym_add_array(c, name, sz->value, 1);
        } else {
            sym = sym_add(c, name, 1);
        }
        if (sym < 0) {
            return -1;
        }
        id = node(c, N_DECL, t);
        if (id < 0) {
            return -1;
        }
        c->nodes[id].value = sym;
        if (take(c, T_ASSIGN)) {
            v = expression(c);
            if (v < 0) {
                return -1;
            }
            c->nodes[id].left = v;
        }
        if (!expect(c, T_SEMI, "expected ; after declaration")) {
            return -1;
        }
        return id;
    }
    if (take(c, T_STRUCT)) {
        Token *tn;
        Token *vn;
        int sid;
        int count = 1;
        StructDef *st;
        uint16_t need;
        tn = cur(c);
        if (!expect(c, T_ID, "expected struct type")) {
            return -1;
        }
        sid = struct_find(c, tn->name);
        if (sid < 0) {
            fail(c, tn->line, tn->column, "unknown struct");
            return -1;
        }
        vn = cur(c);
        if (!expect(c, T_ID, "expected variable name")) {
            return -1;
        }
        st = &c->structs[sid];
        if (take(c, T_LBRACK)) {
            Token *sz = cur(c);
            if (!expect(c, T_NUM, "expected array size") ||
                !expect(c, T_RBRACK, "expected ]")) {
                return -1;
            }
            count = (int)sz->value;
            if (count < 1 || count > 4096) {
                fail(c, sz->line, sz->column, "array size out of range");
                return -1;
            }
        }
        if (sym_find(c, vn->name) >= 0) {
            fail(c, vn->line, vn->column, "duplicate variable");
            return -1;
        }
        if (c->nsyms == SYM_MAX) {
            fail(c, vn->line, vn->column, "symbol table full");
            return -1;
        }
        need = (uint16_t)(count * st->size);
        if (c->mem_next + need > CLVM_MEMORY_SIZE) {
            fail(c, vn->line, vn->column, "struct exceeds memory");
            return -1;
        }
        sym = c->nsyms++;
        text(c->syms[sym].name, NAME_MAX, vn->name);
        c->syms[sym].address = c->mem_next;
        c->syms[sym].is_float = 0;
        c->syms[sym].struct_id = (int8_t)sid;
        c->syms[sym].stride = st->size;
        c->mem_next += need;
        id = node(c, N_DECL, t);
        if (id < 0) {
            return -1;
        }
        c->nodes[id].value = sym;
        c->nodes[id].left = -1;
        if (!expect(c, T_SEMI, "expected ; after struct decl")) {
            return -1;
        }
        return id;
    }
    if (take(c, T_FOR)) {
        id = node(c, N_FOR, t);
        if (id < 0 || !expect(c, T_LP, "expected ( after for")) {
            return -1;
        }
        init = for_init(c);
        if (init < 0 || !expect(c, T_SEMI, "expected ; after for init")) {
            return -1;
        }
        v = expression(c);
        if (v < 0 || !expect(c, T_SEMI, "expected ; after for condition")) {
            return -1;
        }
        step = for_step(c);
        if (step < 0 || !expect(c, T_RP, "expected ) after for")) {
            return -1;
        }
        b = block(c);
        if (b < 0) {
            return -1;
        }
        c->nodes[id].left = init;
        c->nodes[id].right = v;
        c->nodes[id].third = step;
        c->nodes[id].value = b;
        return id;
    }
    if (take(c, T_IF)) {
        id = node(c, N_IF, t);
        if (id < 0 || !expect(c, T_LP, "expected ( after if")) {
            return -1;
        }
        v = expression(c);
        if (v < 0 || !expect(c, T_RP, "expected ) after condition")) {
            return -1;
        }
        b = block(c);
        if (b < 0) {
            return -1;
        }
        c->nodes[id].left = v;
        c->nodes[id].right = b;
        if (take(c, T_ELSE)) {
            b = block(c);
            if (b < 0) {
                return -1;
            }
            c->nodes[id].third = b;
        }
        return id;
    }
    if (take(c, T_WHILE)) {
        id = node(c, N_WHILE, t);
        if (id < 0 || !expect(c, T_LP, "expected ( after while")) {
            return -1;
        }
        v = expression(c);
        if (v < 0 || !expect(c, T_RP, "expected ) after condition")) {
            return -1;
        }
        b = block(c);
        if (b < 0) {
            return -1;
        }
        c->nodes[id].left = v;
        c->nodes[id].right = b;
        return id;
    }
    if (take(c, T_RETURN)) {
        id = node(c, N_RETURN, t);
        if (id < 0) {
            return -1;
        }
        c->nodes[id].value = c->cur_fn;
        if (cur(c)->kind != T_SEMI) {
            v = expression(c);
            if (v < 0) {
                return -1;
            }
            c->nodes[id].left = v;
        }
        if (!expect(c, T_SEMI, "expected ; after return")) {
            return -1;
        }
        return id;
    }
    if (cur(c)->kind == T_ID &&
        (c->tokens[c->pos + 1].kind == T_ASSIGN ||
         c->tokens[c->pos + 1].kind == T_LBRACK ||
         c->tokens[c->pos + 1].kind == T_DOT)) {
        name = cur(c);
        ++c->pos;
        sym = sym_find(c, name->name);
        if (sym < 0) {
            fail(c, name->line, name->column, "unknown variable");
            return -1;
        }
        if (take(c, T_LBRACK)) {
            int idx = expression(c);
            if (idx < 0 || !expect(c, T_RBRACK, "expected ]")) {
                return -1;
            }
            if (take(c, T_DOT)) {
                Token *fld = cur(c);
                int fi;
                if (c->syms[sym].struct_id < 0) {
                    fail(c, name->line, name->column, "not a struct");
                    return -1;
                }
                if (!expect(c, T_ID, "expected field") ||
                    !expect(c, T_ASSIGN, "expected =")) {
                    return -1;
                }
                fi = field_find(&c->structs[c->syms[sym].struct_id], fld->name);
                if (fi < 0) {
                    fail(c, fld->line, fld->column, "unknown field");
                    return -1;
                }
                id = node(c, N_FIELD_ASSIGN, name);
                if (id < 0) {
                    return -1;
                }
                v = expression(c);
                if (v < 0) {
                    return -1;
                }
                c->nodes[id].value = sym;
                c->nodes[id].left = v;
                c->nodes[id].right = idx;
                c->nodes[id].third = fi;
            } else {
                if (!expect(c, T_ASSIGN, "expected =")) {
                    return -1;
                }
                id = node(c, N_INDEX_ASSIGN, name);
                if (id < 0) {
                    return -1;
                }
                v = expression(c);
                if (v < 0) {
                    return -1;
                }
                c->nodes[id].value = sym;
                c->nodes[id].left = v;
                c->nodes[id].right = idx;
            }
        } else if (take(c, T_DOT)) {
            Token *fld = cur(c);
            int fi;
            if (c->syms[sym].struct_id < 0) {
                fail(c, name->line, name->column, "not a struct");
                return -1;
            }
            if (!expect(c, T_ID, "expected field") ||
                !expect(c, T_ASSIGN, "expected =")) {
                return -1;
            }
            fi = field_find(&c->structs[c->syms[sym].struct_id], fld->name);
            if (fi < 0) {
                fail(c, fld->line, fld->column, "unknown field");
                return -1;
            }
            id = node(c, N_FIELD_ASSIGN, name);
            if (id < 0) {
                return -1;
            }
            v = expression(c);
            if (v < 0) {
                return -1;
            }
            c->nodes[id].value = sym;
            c->nodes[id].left = v;
            c->nodes[id].right = -1;
            c->nodes[id].third = fi;
        } else {
            if (!expect(c, T_ASSIGN, "expected =")) {
                return -1;
            }
            id = node(c, N_ASSIGN, name);
            if (id < 0) {
                return -1;
            }
            v = expression(c);
            if (v < 0) {
                return -1;
            }
            c->nodes[id].value = sym;
            c->nodes[id].left = v;
        }
        if (!expect(c, T_SEMI, "expected ; after assignment")) {
            return -1;
        }
        return id;
    }
    id = node(c, N_EXPR, t);
    if (id < 0) {
        return -1;
    }
    v = expression(c);
    if (v < 0) {
        return -1;
    }
    c->nodes[id].left = v;
    if (!expect(c, T_SEMI, "expected ; after expression")) {
        return -1;
    }
    return id;
}

static int block(Compiler *c) {
    Token *t = cur(c);
    int id;
    int first = -1;
    int last = -1;
    int s;

    if (!expect(c, T_LB, "expected {")) {
        return -1;
    }
    id = node(c, N_BLOCK, t);
    if (id < 0) {
        return -1;
    }
    while (cur(c)->kind != T_RB && cur(c)->kind != T_EOF) {
        s = statement(c);
        if (s < 0) {
            return -1;
        }
        if (first < 0) {
            first = s;
        } else {
            c->nodes[last].next = s;
        }
        last = s;
    }
    if (!expect(c, T_RB, "expected }")) {
        return -1;
    }
    c->nodes[id].left = first;
    return id;
}

static int parse_function(Compiler *c, uint8_t ret, Token *name) {
    FuncDef *f;
    if (c->nfuncs == FUNC_MAX) {
        return fail(c, name->line, name->column, "too many functions");
    }
    if (func_find(c, name->name) >= 0) {
        return fail(c, name->line, name->column, "duplicate function");
    }
    f = &c->funcs[c->nfuncs++];
    text(f->name, NAME_MAX, name->name);
    f->argc = 0;
    f->ret = ret;
    f->body = -1;
    f->entry = -1;
    f->is_main = (uint8_t)same(name->name, "main");
    c->cur_fn = c->nfuncs - 1;
    c->scope_base = c->nsyms;
    if (!expect(c, T_LP, "expected (")) {
        return 0;
    }
    if (!take(c, T_RP)) {
        do {
            uint8_t isf = 0;
            Token *an;
            int sym;
            if (take(c, T_FLOAT)) {
                isf = 1;
            } else if (!take(c, T_INT)) {
                return fail(c, cur(c)->line, cur(c)->column, "expected arg type");
            }
            an = cur(c);
            if (!expect(c, T_ID, "expected arg name")) {
                return 0;
            }
            if (f->argc == 8) {
                return fail(c, an->line, an->column, "too many args");
            }
            sym = sym_add(c, an, isf);
            if (sym < 0) {
                return 0;
            }
            f->arg_float[f->argc] = isf;
            f->arg_addr[f->argc] = c->syms[sym].address;
            f->argc++;
        } while (take(c, T_COMMA));
        if (!expect(c, T_RP, "expected )")) {
            return 0;
        }
    }
    f->body = block(c);
    if (f->body < 0) {
        return 0;
    }
    return 1;
}

static int program(Compiler *c) {
    int main_i = -1;
    while (cur(c)->kind != T_EOF) {
        uint8_t ret;
        Token *name;
        if (take(c, T_STRUCT)) {
            if (!parse_struct_def(c)) {
                return -1;
            }
            continue;
        }
        if (take(c, T_VOID)) {
            ret = 0;
        } else if (take(c, T_INT)) {
            ret = 1;
        } else if (take(c, T_FLOAT)) {
            ret = 2;
        } else {
            fail(c, cur(c)->line, cur(c)->column, "expected function or struct");
            return -1;
        }
        name = cur(c);
        if (!expect(c, T_ID, "expected function name")) {
            return -1;
        }
        if (!parse_function(c, ret, name)) {
            return -1;
        }
        if (c->funcs[c->nfuncs - 1].is_main) {
            main_i = c->nfuncs - 1;
        }
    }
    if (main_i < 0) {
        fail(c, 1, 1, "expected main");
        return -1;
    }
    return c->funcs[main_i].body;
}

static int room(Compiler *c, size_t n, Node *x) {
    if (c->pc + n <= c->cap && c->pc + n <= 65535u) {
        return 1;
    }
    return fail(c, x->line, x->column, "bytecode output full");
}

static int byte(Compiler *c, uint8_t v, Node *x) {
    if (!room(c, 1, x)) {
        return 0;
    }
    c->out[c->pc++] = v;
    return 1;
}

static int push(Compiler *c, int32_t v, Node *x) {
    uint32_t u = (uint32_t)v;
    if (!room(c, 5, x)) {
        return 0;
    }
    c->out[c->pc++] = CL_OP_PUSH;
    c->out[c->pc++] = (uint8_t)u;
    c->out[c->pc++] = (uint8_t)(u >> 8);
    c->out[c->pc++] = (uint8_t)(u >> 16);
    c->out[c->pc++] = (uint8_t)(u >> 24);
    return 1;
}

static int fpush(Compiler *c, int32_t bits, Node *x) {
    uint32_t u = (uint32_t)bits;
    if (!room(c, 5, x)) {
        return 0;
    }
    c->out[c->pc++] = CL_OP_FPUSH;
    c->out[c->pc++] = (uint8_t)u;
    c->out[c->pc++] = (uint8_t)(u >> 8);
    c->out[c->pc++] = (uint8_t)(u >> 16);
    c->out[c->pc++] = (uint8_t)(u >> 24);
    return 1;
}

static int branch(Compiler *c, uint8_t op, Node *x) {
    int p = (int)c->pc;
    if (!room(c, 3, x)) {
        return -1;
    }
    c->out[c->pc++] = op;
    c->out[c->pc++] = 0;
    c->out[c->pc++] = 0;
    return p;
}

static int patch(Compiler *c, int p, size_t target, Node *x) {
    int32_t rel = (int32_t)target - (p + 3);
    if (rel < -32768 || rel > 32767) {
        return fail(c, x->line, x->column, "branch too far");
    }
    c->out[p + 1] = (uint8_t)rel;
    c->out[p + 2] = (uint8_t)((uint32_t)rel >> 8);
    return 1;
}

static int gen_expr(Compiler *c, int id);

static int gen_float_binop(Compiler *c, Node *n, uint8_t op) {
    int lf = c->nodes[n->left].is_float;
    int rf = c->nodes[n->right].is_float;
    int a;
    int b;

    a = gen_expr(c, n->left);
    if (a != 1) {
        return -1;
    }
    if (!lf && rf && !byte(c, CL_OP_ITOF, n)) {
        return -1;
    }
    b = gen_expr(c, n->right);
    if (b != 1) {
        return -1;
    }
    if (lf && !rf && !byte(c, CL_OP_ITOF, n)) {
        return -1;
    }
    return byte(c, op, n) ? 1 : -1;
}

static int gen_index_addr(Compiler *c, int sym, int idx_id, Node *n) {
    if (gen_expr(c, idx_id) != 1) {
        return -1;
    }
    if (!push(c, (int32_t)c->syms[sym].stride, n) || !byte(c, CL_OP_MUL, n)) {
        return -1;
    }
    if (!push(c, c->syms[sym].address, n) || !byte(c, CL_OP_ADD, n)) {
        return -1;
    }
    return 1;
}

static int gen_field_addr(Compiler *c, int sym, int fi, int idx_id, Node *n) {
    uint16_t off = (uint16_t)(fi * 4);
    if (idx_id >= 0) {
        if (gen_index_addr(c, sym, idx_id, n) != 1) {
            return -1;
        }
        if (off != 0 && (!push(c, (int32_t)off, n) || !byte(c, CL_OP_ADD, n))) {
            return -1;
        }
        return 1;
    }
    return push(c, (int32_t)c->syms[sym].address + (int32_t)off, n) ? 1 : -1;
}

static int emit_call(Compiler *c, int fn, Node *n) {
    int p = branch(c, CL_OP_CALL, n);
    if (p < 0) {
        return -1;
    }
    if (c->funcs[fn].entry >= 0) {
        return patch(c, p, (size_t)c->funcs[fn].entry, n) ? 1 : -1;
    }
    if (c->npatches == CALL_PATCH_MAX) {
        fail(c, n->line, n->column, "too many calls");
        return -1;
    }
    c->patches[c->npatches].at = p;
    c->patches[c->npatches].fn = fn;
    c->npatches++;
    return 1;
}

static int gen_call(Compiler *c, Node *n) {
    const Builtin *b = builtin(n->name);
    int i;
    int leaves;
    int fn;

    if (!b) {
        fn = func_find(c, n->name);
        if (fn < 0) {
            fail(c, n->line, n->column, "unknown function");
            return -1;
        }
        if (n->value != c->funcs[fn].argc) {
            fail(c, n->line, n->column, "wrong argument count");
            return -1;
        }
        for (i = 0; i < n->value; ++i) {
            leaves = gen_expr(c, c->args[n->left + i]);
            if (leaves != 1) {
                fail(c, n->line, n->column, "argument has no value");
                return -1;
            }
            if (!push(c, c->funcs[fn].arg_addr[i], n)) {
                return -1;
            }
            if (!byte(c, c->funcs[fn].arg_float[i] ? CL_OP_FSTORE : CL_OP_STORE, n)) {
                return -1;
            }
        }
        if (emit_call(c, fn, n) != 1) {
            return -1;
        }
        n->is_float = (uint8_t)(c->funcs[fn].ret == 2);
        return c->funcs[fn].ret ? 1 : 0;
    }
    if (n->value != b->argc) {
        fail(c, n->line, n->column, "wrong builtin argument count");
        return -1;
    }
    for (i = 0; i < n->value; ++i) {
        leaves = gen_expr(c, c->args[n->left + i]);
        if (leaves != 1) {
            fail(c, n->line, n->column, "argument has no value");
            return -1;
        }
    }
    if (!push(c, b->id, n) || !byte(c, CL_OP_SYS, n)) {
        return -1;
    }
    if (b->id == 31 || b->id == 32) {
        n->is_float = 1;
    }
    return b->returns ? 1 : 0;
}

static int gen_expr(Compiler *c, int id) {
    Node *n = &c->nodes[id];
    int a;
    int b;
    uint8_t op = 0;

    if (n->kind == N_INT) {
        return push(c, n->value, n) ? 1 : -1;
    }
    if (n->kind == N_FLOAT) {
        return fpush(c, n->value, n) ? 1 : -1;
    }
    if (n->kind == N_VAR) {
        if (n->is_float) {
            return push(c, c->syms[n->value].address, n) &&
                   byte(c, CL_OP_FLOAD, n) ? 1 : -1;
        }
        return push(c, c->syms[n->value].address, n) && byte(c, CL_OP_LOAD, n) ? 1 : -1;
    }
    if (n->kind == N_INDEX) {
        if (gen_index_addr(c, n->value, n->left, n) != 1) {
            return -1;
        }
        return byte(c, n->is_float ? CL_OP_FLOAD : CL_OP_LOAD, n) ? 1 : -1;
    }
    if (n->kind == N_FIELD) {
        if (gen_field_addr(c, n->value, n->left, n->right, n) != 1) {
            return -1;
        }
        return byte(c, n->is_float ? CL_OP_FLOAD : CL_OP_LOAD, n) ? 1 : -1;
    }
    if (n->kind == N_CALL) {
        return gen_call(c, n);
    }
    if (n->kind == N_NEG) {
        a = gen_expr(c, n->left);
        if (a != 1) {
            return -1;
        }
        return byte(c, n->is_float ? CL_OP_FNEG : CL_OP_NEG, n) ? 1 : -1;
    }
    if (n->is_float) {
        switch (n->kind) {
        case N_ADD: return gen_float_binop(c, n, CL_OP_FADD);
        case N_SUB: return gen_float_binop(c, n, CL_OP_FSUB);
        case N_MUL: return gen_float_binop(c, n, CL_OP_FMUL);
        case N_DIV: return gen_float_binop(c, n, CL_OP_FDIV);
        case N_EQ:  return gen_float_binop(c, n, CL_OP_FEQ);
        case N_LT:  return gen_float_binop(c, n, CL_OP_FLT);
        case N_LE:  return gen_float_binop(c, n, CL_OP_FLE);
        case N_GT:
            b = gen_expr(c, n->right);
            if (b != 1) {
                return -1;
            }
            if (!c->nodes[n->right].is_float && !byte(c, CL_OP_ITOF, n)) {
                return -1;
            }
            a = gen_expr(c, n->left);
            if (a != 1) {
                return -1;
            }
            if (!c->nodes[n->left].is_float && !byte(c, CL_OP_ITOF, n)) {
                return -1;
            }
            return byte(c, CL_OP_FLT, n) ? 1 : -1;
        case N_GE:
            b = gen_expr(c, n->right);
            if (b != 1) {
                return -1;
            }
            if (!c->nodes[n->right].is_float && !byte(c, CL_OP_ITOF, n)) {
                return -1;
            }
            a = gen_expr(c, n->left);
            if (a != 1) {
                return -1;
            }
            if (!c->nodes[n->left].is_float && !byte(c, CL_OP_ITOF, n)) {
                return -1;
            }
            return byte(c, CL_OP_FLE, n) ? 1 : -1;
        case N_NE:
            if (gen_float_binop(c, n, CL_OP_FEQ) != 1) {
                return -1;
            }
            return push(c, 1, n) && byte(c, CL_OP_SWAP, n) && byte(c, CL_OP_SUB, n) ? 1 : -1;
        case N_MOD:
            fail(c, n->line, n->column, "mod not defined for float");
            return -1;
        default:
            fail(c, n->line, n->column, "bad expression node");
            return -1;
        }
    }
    a = gen_expr(c, n->left);
    b = gen_expr(c, n->right);
    if (a != 1 || b != 1) {
        fail(c, n->line, n->column, "operator needs values");
        return -1;
    }
    switch (n->kind) {
    case N_ADD: op = CL_OP_ADD; break;
    case N_SUB: op = CL_OP_SUB; break;
    case N_MUL: op = CL_OP_MUL; break;
    case N_DIV: op = CL_OP_DIV; break;
    case N_MOD: op = CL_OP_MOD; break;
    case N_EQ:  op = CL_OP_EQ;  break;
    case N_NE:  op = CL_OP_NE;  break;
    case N_LT:  op = CL_OP_LT;  break;
    case N_LE:  op = CL_OP_LE;  break;
    case N_GT:  op = CL_OP_GT;  break;
    case N_GE:  op = CL_OP_GE;  break;
    default:
        fail(c, n->line, n->column, "bad expression node");
        return -1;
    }
    return byte(c, op, n) ? 1 : -1;
}

static int gen_block(Compiler *c, int id);

static int gen_stmt(Compiler *c, int id) {
    Node *n = &c->nodes[id];
    int v;
    int p;
    int q;

    if (n->kind == N_DECL) {
        int is_float = c->syms[n->value].is_float;
        if (c->syms[n->value].struct_id >= 0 && n->left < 0) {
            return 1;
        }
        if (n->left >= 0) {
            v = gen_expr(c, n->left);
            if (v != 1) {
                return 0;
            }
        } else if (is_float) {
            if (!fpush(c, 0, n)) {
                return 0;
            }
        } else if (!push(c, 0, n)) {
            return 0;
        }
        return push(c, c->syms[n->value].address, n) &&
               byte(c, is_float ? CL_OP_FSTORE : CL_OP_STORE, n);
    }
    if (n->kind == N_ASSIGN) {
        v = gen_expr(c, n->left);
        return v == 1 && push(c, c->syms[n->value].address, n) &&
               byte(c, c->syms[n->value].is_float ? CL_OP_FSTORE : CL_OP_STORE, n);
    }
    if (n->kind == N_INDEX_ASSIGN) {
        v = gen_expr(c, n->left);
        if (v != 1) {
            return 0;
        }
        if (gen_index_addr(c, n->value, n->right, n) != 1) {
            return 0;
        }
        return byte(c, c->syms[n->value].is_float ? CL_OP_FSTORE : CL_OP_STORE, n);
    }
    if (n->kind == N_FIELD_ASSIGN) {
        int sid = c->syms[n->value].struct_id;
        uint8_t isf;
        if (sid < 0 || n->third < 0) {
            return fail(c, n->line, n->column, "bad field assign");
        }
        isf = c->structs[sid].is_float[n->third];
        v = gen_expr(c, n->left);
        if (v != 1) {
            return 0;
        }
        if (gen_field_addr(c, n->value, n->third, n->right, n) != 1) {
            return 0;
        }
        return byte(c, isf ? CL_OP_FSTORE : CL_OP_STORE, n);
    }
    if (n->kind == N_EXPR) {
        v = gen_expr(c, n->left);
        return v >= 0 && (v == 0 || byte(c, CL_OP_DROP, n));
    }
    if (n->kind == N_RETURN) {
        int fn = n->value;
        int is_main = (fn >= 0 && fn < c->nfuncs) ? c->funcs[fn].is_main : 1;
        if (n->left >= 0) {
            v = gen_expr(c, n->left);
            if (v != 1) {
                return 0;
            }
            if (is_main && !byte(c, CL_OP_DROP, n)) {
                return 0;
            }
        }
        return byte(c, is_main ? CL_OP_HALT : CL_OP_RET, n);
    }
    if (n->kind == N_IF) {
        v = gen_expr(c, n->left);
        if (v != 1) {
            return 0;
        }
        p = branch(c, CL_OP_JZ, n);
        if (p < 0) {
            return 0;
        }
        if (!gen_block(c, n->right)) {
            return 0;
        }
        if (n->third >= 0) {
            q = branch(c, CL_OP_JMP, n);
            if (q < 0 || !patch(c, p, c->pc, n)) {
                return 0;
            }
            if (!gen_block(c, n->third) || !patch(c, q, c->pc, n)) {
                return 0;
            }
        } else if (!patch(c, p, c->pc, n)) {
            return 0;
        }
        return 1;
    }
    if (n->kind == N_WHILE) {
        size_t begin = c->pc;
        v = gen_expr(c, n->left);
        if (v != 1) {
            return 0;
        }
        p = branch(c, CL_OP_JZ, n);
        if (p < 0 || !gen_block(c, n->right)) {
            return 0;
        }
        q = branch(c, CL_OP_JMP, n);
        if (q < 0 || !patch(c, q, begin, n) || !patch(c, p, c->pc, n)) {
            return 0;
        }
        return 1;
    }
    if (n->kind == N_FOR) {
        size_t begin;
        if (!gen_stmt(c, n->left)) {
            return 0;
        }
        begin = c->pc;
        v = gen_expr(c, n->right);
        if (v != 1) {
            return 0;
        }
        p = branch(c, CL_OP_JZ, n);
        if (p < 0 || !gen_block(c, n->value)) {
            return 0;
        }
        if (!gen_stmt(c, n->third)) {
            return 0;
        }
        q = branch(c, CL_OP_JMP, n);
        if (q < 0 || !patch(c, q, begin, n) || !patch(c, p, c->pc, n)) {
            return 0;
        }
        return 1;
    }
    return fail(c, n->line, n->column, "bad statement node");
}

static int gen_block(Compiler *c, int id) {
    int s = c->nodes[id].left;
    while (s >= 0) {
        if (!gen_stmt(c, s)) {
            return 0;
        }
        s = c->nodes[s].next;
    }
    return 1;
}

int chrisc_compile(const char *source, size_t source_size, uint8_t *code,
                   size_t code_cap, ChrisResult *result) {
    Compiler *c = &g_chrisc;
    int root;
    int i;
    int main_i = -1;

    if (!source || !code || !result) {
        return 0;
    }
    c->ntok = c->pos = c->nnode = c->nargs = c->nsyms = 0;
    c->nstructs = c->nfuncs = c->npatches = c->cur_fn = 0;
    c->scope_base = 0;
    c->mem_next = 0;
    c->out = code;
    c->cap = code_cap;
    c->pc = 0;
    c->result = result;
    result->code_size = 0;
    result->entry = 0;
    result->variables = 0;
    result->diag.line = result->diag.column = 0;
    result->diag.message[0] = 0;
    if (!lex(c, source, source_size)) {
        return 0;
    }
    root = program(c);
    if (root < 0) {
        return 0;
    }
    for (i = 0; i < c->nfuncs; ++i) {
        Node *body = &c->nodes[c->funcs[i].body];
        uint8_t end_op;
        c->cur_fn = i;
        c->funcs[i].entry = (int)c->pc;
        if (!gen_block(c, c->funcs[i].body)) {
            return 0;
        }
        end_op = c->funcs[i].is_main ? CL_OP_HALT : CL_OP_RET;
        if (c->pc == 0 || c->out[c->pc - 1] != end_op) {
            if (!byte(c, end_op, body)) {
                return 0;
            }
        }
        if (c->funcs[i].is_main) {
            main_i = i;
        }
    }
    for (i = 0; i < c->npatches; ++i) {
        int fn = c->patches[i].fn;
        if (!patch(c, c->patches[i].at, (size_t)c->funcs[fn].entry,
                   &c->nodes[c->funcs[fn].body])) {
            return 0;
        }
    }
    result->code_size = c->pc;
    result->variables = (unsigned)c->nsyms;
    if (main_i >= 0) {
        result->entry = (uint16_t)c->funcs[main_i].entry;
    }
    (void)root;
    return 1;
}
