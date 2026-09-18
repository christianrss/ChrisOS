#include "chrisc.h"
#include "../clvm/clvm.h"

#define TOK_MAX 4096
#define NODE_MAX 2048
#define ARG_MAX 4096
#define SYM_MAX 128
#define NAME_MAX 24

typedef enum TokenKind {
    T_EOF = 0, T_ID, T_NUM, T_VOID, T_INT, T_IF, T_ELSE, T_WHILE, T_RETURN,
    T_LP, T_RP, T_LB, T_RB, T_SEMI, T_COMMA, T_ASSIGN, T_PLUS, T_MINUS,
    T_STAR, T_SLASH, T_PERCENT, T_EQ, T_NE, T_LT, T_LE, T_GT, T_GE
} TokenKind;

typedef struct Token {
    TokenKind kind;
    int32_t value;
    int line, column;
    char name[NAME_MAX];
} Token;

typedef enum NodeKind {
    N_BLOCK, N_DECL, N_ASSIGN, N_IF, N_WHILE, N_RETURN, N_EXPR,
    N_INT, N_VAR, N_CALL, N_NEG, N_ADD, N_SUB, N_MUL, N_DIV, N_MOD,
    N_EQ, N_NE, N_LT, N_LE, N_GT, N_GE
} NodeKind;

typedef struct Node {
    NodeKind kind;
    int left, right, third, next;
    int32_t value;
    int line, column;
    char name[NAME_MAX];
} Node;

typedef struct Symbol {
    char name[NAME_MAX];
    uint16_t address;
} Symbol;

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
    uint8_t *out;
    size_t cap, pc;
    ChrisResult *result;
} Compiler;

static const Builtin builtins[] = {
    {"pixel", 1, 3, 0}, {"rect", 2, 5, 0}, {"line", 3, 5, 0},
    {"sprite", 4, 6, 0}, {"tilemap", 5, 7, 0}, {"clear", 6, 1, 0},
    {"key", 10, 1, 1}, {"ticks", 11, 0, 1}, {"wait", 12, 1, 0},
    {"tone", 13, 2, 0}, {"tri", 20, 10, 0}, {"mesh", 21, 5, 0}
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
    if (same(s, "if")) {
        return T_IF;
    }
    if (same(s, "else")) {
        return T_ELSE;
    }
    if (same(s, "while")) {
        return T_WHILE;
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
        ONE(';', T_SEMI);
        ONE(',', T_COMMA);
        ONE('+', T_PLUS);
        ONE('-', T_MINUS);
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
    n->name[0] = 0;
    return id;
}

static int sym_find(Compiler *c, const char *name) {
    int i;
    for (i = 0; i < c->nsyms; ++i) {
        if (same(c->syms[i].name, name)) {
            return i;
        }
    }
    return -1;
}

static int sym_add(Compiler *c, Token *t) {
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
    c->syms[i].address = (uint16_t)(i * 4);
    return i;
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
    id = node(c, N_VAR, t);
    if (id >= 0) {
        c->nodes[id].value = sym;
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

static int statement(Compiler *c) {
    Token *t = cur(c);
    Token *name;
    int id;
    int sym;
    int v;
    int b;

    if (take(c, T_INT)) {
        name = cur(c);
        if (!expect(c, T_ID, "expected variable name")) {
            return -1;
        }
        sym = sym_add(c, name);
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
    if (cur(c)->kind == T_ID && c->tokens[c->pos + 1].kind == T_ASSIGN) {
        name = cur(c);
        c->pos += 2;
        sym = sym_find(c, name->name);
        if (sym < 0) {
            fail(c, name->line, name->column, "unknown variable");
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

static int program(Compiler *c) {
    int root;

    if (!expect(c, T_VOID, "program must start with void")) {
        return -1;
    }
    if (cur(c)->kind != T_ID || !same(cur(c)->name, "main")) {
        fail(c, cur(c)->line, cur(c)->column, "expected main");
        return -1;
    }
    ++c->pos;
    if (!expect(c, T_LP, "expected (") || !expect(c, T_RP, "expected )")) {
        return -1;
    }
    root = block(c);
    if (root < 0 || !expect(c, T_EOF, "text after main block")) {
        return -1;
    }
    return root;
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

static int gen_call(Compiler *c, Node *n) {
    const Builtin *b = builtin(n->name);
    int i;
    int leaves;

    if (!b) {
        return fail(c, n->line, n->column, "unknown builtin");
    }
    if (n->value != b->argc) {
        return fail(c, n->line, n->column, "wrong builtin argument count");
    }
    for (i = 0; i < n->value; ++i) {
        leaves = gen_expr(c, c->args[n->left + i]);
        if (leaves != 1) {
            return fail(c, n->line, n->column, "argument has no value");
        }
    }
    if (!push(c, b->id, n) || !byte(c, CL_OP_SYS, n)) {
        return -1;
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
    if (n->kind == N_VAR) {
        return push(c, c->syms[n->value].address, n) && byte(c, CL_OP_LOAD, n) ? 1 : -1;
    }
    if (n->kind == N_CALL) {
        return gen_call(c, n);
    }
    if (n->kind == N_NEG) {
        a = gen_expr(c, n->left);
        return a == 1 && byte(c, CL_OP_NEG, n) ? 1 : -1;
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
        if (n->left >= 0) {
            v = gen_expr(c, n->left);
            if (v != 1) {
                return 0;
            }
        } else if (!push(c, 0, n)) {
            return 0;
        }
        return push(c, c->syms[n->value].address, n) && byte(c, CL_OP_STORE, n);
    }
    if (n->kind == N_ASSIGN) {
        v = gen_expr(c, n->left);
        return v == 1 && push(c, c->syms[n->value].address, n) && byte(c, CL_OP_STORE, n);
    }
    if (n->kind == N_EXPR) {
        v = gen_expr(c, n->left);
        return v >= 0 && (v == 0 || byte(c, CL_OP_DROP, n));
    }
    if (n->kind == N_RETURN) {
        if (n->left >= 0) {
            v = gen_expr(c, n->left);
            if (v != 1 || !byte(c, CL_OP_DROP, n)) {
                return 0;
            }
        }
        return byte(c, CL_OP_HALT, n);
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
    Compiler c;
    int root;

    if (!source || !code || !result) {
        return 0;
    }
    c.ntok = c.pos = c.nnode = c.nargs = c.nsyms = 0;
    c.out = code;
    c.cap = code_cap;
    c.pc = 0;
    c.result = result;
    result->code_size = 0;
    result->entry = 0;
    result->variables = 0;
    result->diag.line = result->diag.column = 0;
    result->diag.message[0] = 0;
    if (!lex(&c, source, source_size)) {
        return 0;
    }
    root = program(&c);
    if (root < 0) {
        return 0;
    }
    if (!gen_block(&c, root)) {
        return 0;
    }
    if (c.pc == 0 || c.out[c.pc - 1] != CL_OP_HALT) {
        if (!byte(&c, CL_OP_HALT, &c.nodes[root])) {
            return 0;
        }
    }
    result->code_size = c.pc;
    result->variables = (unsigned)c.nsyms;
    return 1;
}
