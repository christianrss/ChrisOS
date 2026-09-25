#ifndef CHRIS_SH_INT_H
#define CHRIS_SH_INT_H

#include "sh_pub.h"

#define SH_TOK_MAX 768
#define SH_AST_MAX 512
#define SH_SYM_MAX 64
#define SH_IR_MAX 384
#define SH_IMM_MAX 160
#define SH_ERR_MAX 8
#define SH_TEMP_MAX 96
#define SH_NEST_MAX 32
#define SH_TGSI_MAX 3600
#define SH_LOG_MAX 1536
#define SH_NAME_MAX 40
#define SH_SCOPE_MAX 48

enum {
    TK_EOF = 0,
    TK_IDENT,
    TK_INT,
    TK_FLOAT,
    TK_LPAREN,
    TK_RPAREN,
    TK_LBRACE,
    TK_RBRACE,
    TK_LBRACK,
    TK_RBRACK,
    TK_SEMI,
    TK_COMMA,
    TK_DOT,
    TK_PLUS,
    TK_MINUS,
    TK_STAR,
    TK_SLASH,
    TK_PERCENT,
    TK_ASSIGN,
    TK_EQ,
    TK_NE,
    TK_LT,
    TK_GT,
    TK_LE,
    TK_GE,
    TK_AND,
    TK_OR,
    TK_NOT,
    TK_INC,
    TK_DEC,
    TK_IN,
    TK_OUT,
    TK_UNIFORM,
    TK_CONST,
    TK_LAYOUT,
    TK_SMOOTH,
    TK_VOID,
    TK_BOOL,
    TK_INTKW,
    TK_FLOATKW,
    TK_VEC2,
    TK_VEC3,
    TK_VEC4,
    TK_IVEC2,
    TK_IVEC3,
    TK_IVEC4,
    TK_MAT3,
    TK_MAT4,
    TK_SAMPLER2D,
    TK_IF,
    TK_ELSE,
    TK_FOR,
    TK_RETURN,
    TK_DISCARD,
    TK_TRUE,
    TK_FALSE,
    TK_BAD
};

enum {
    Q_IN = 1,
    Q_OUT = 2,
    Q_UNIFORM = 4,
    Q_CONST = 8,
    Q_SMOOTH = 16
};

enum {
    NK_DECL = 1,
    NK_FUNC,
    NK_BLOCK,
    NK_RETURN,
    NK_IF,
    NK_FOR,
    NK_EXPR,
    NK_ASSIGN,
    NK_DISCARD,
    NK_EMPTY,
    NK_LIT,
    NK_IDENT,
    NK_UNARY,
    NK_BINARY,
    NK_CALL,
    NK_SWZ,
    NK_INDEX,
    NK_CTOR,
    NK_POST,
    NK_PRE
};

enum {
    SYM_IN = 1,
    SYM_OUT,
    SYM_UNIFORM,
    SYM_LOCAL,
    SYM_FUNC,
    SYM_POS,
    SYM_FCOORD
};

enum {
    IR_NOP = 0,
    IR_CONST,
    IR_MOV,
    IR_SWZ,
    IR_SETLANE,
    IR_ADD,
    IR_SUB,
    IR_MUL,
    IR_MAX,
    IR_MIN,
    IR_ABS,
    IR_DOT,
    IR_RSQ,
    IR_RCP,
    IR_SIN,
    IR_COS,
    IR_POW,
    IR_TRUNC,
    IR_CMP,
    IR_MULMV,
    IR_MULMM,
    IR_SAMPLE,
    IR_LOAD_ATTR,
    IR_LOAD_VAR,
    IR_LOAD_UNI,
    IR_LOAD_FCOORD,
    IR_STORE_POS,
    IR_STORE_VAR,
    IR_STORE_COLOR,
    IR_IF,
    IR_ELSE,
    IR_ENDIF,
    IR_DISCARD
};

enum { CMP_LT = 1, CMP_GT, CMP_LE, CMP_GE, CMP_EQ, CMP_NE };

typedef struct Tok {
    uint16_t kind;
    uint16_t len;
    uint32_t off;
    uint16_t line;
    uint16_t col;
    uint32_t ival;
    float fval;
} Tok;

typedef struct Ast {
    uint8_t kind;
    uint8_t op;
    uint8_t ty;
    uint8_t quals;
    int16_t a, b, c, d;
    int16_t next;
    int16_t tok;
    int32_t i;
    float f;
} Ast;

typedef struct Sym {
    char name[SH_NAME_MAX];
    uint8_t kind;
    uint8_t ty;
    int8_t loc;
    int16_t slot;
    int16_t scope;
    int16_t tmp;
    int16_t body;
    int16_t params;
    uint8_t nparam;
    uint8_t pty[4];
    uint8_t is_const;
    uint8_t known;
    uint8_t wrote;
    float cv;
} Sym;

typedef struct Ir {
    uint8_t op;
    uint8_t ty;
    uint8_t ncomp;
    uint8_t pad;
    int16_t dst;
    int16_t a, b, c;
    int16_t aux;
    int16_t aux2;
} Ir;

typedef struct ShVal {
    int16_t tmp;
    uint8_t ty;
    uint8_t ncomp;
    uint8_t cols;
    uint8_t is_const;
    uint8_t samp;
    float cv[4];
} ShVal;

typedef struct ShComp {
    char name[64];
    char src[SH_SRC_MAX];
    int src_len;
    int stage;
    int version;
    Tok tok[SH_TOK_MAX];
    int ntok;
    int tp;
    Ast ast[SH_AST_MAX];
    int nast;
    int root;
    Sym sym[SH_SYM_MAX];
    int nsym;
    int cur_scope;
    int nscope;
    int scope_parent[SH_SCOPE_MAX];
    Ir ir[SH_IR_MAX];
    int nir;
    float imm[SH_IMM_MAX];
    int nimm;
    int ntmp;
    int nerr;
    int depth;
    int wrote_pos;
    int nout;
    int uni_hi;
    int attr_hi;
    int var_hi;
    int samp_hi;
    int fn_sp;
    int fn_stk[8];
    int ret_set;
    ShVal ret_val;
    char log[SH_LOG_MAX];
    int logn;
    uint64_t cyc_lex, cyc_parse, cyc_sem, cyc_ir, cyc_tgsi;
} ShComp;

extern void *memcpy(void *dst, const void *src, unsigned long n);
extern void *memset(void *dst, int c, unsigned long n);

static inline void sh_app(char *d, int cap, int *n, const char *s) {
    if (!s) {
        return;
    }
    while (*s && *n + 1 < cap) {
        d[(*n)++] = *s++;
    }
    if (cap > 0 && *n < cap) {
        d[*n] = 0;
    }
}

static inline void sh_app_ch(char *d, int cap, int *n, char ch) {
    if (*n + 1 < cap) {
        d[(*n)++] = ch;
    }
    if (cap > 0 && *n < cap) {
        d[*n] = 0;
    }
}

static inline void sh_app_u(char *d, int cap, int *n, unsigned v) {
    char tmp[16];
    int i = 0;
    if (v == 0u) {
        sh_app_ch(d, cap, n, '0');
        return;
    }
    while (v && i < 16) {
        tmp[i++] = (char)('0' + (v % 10u));
        v /= 10u;
    }
    while (i > 0) {
        sh_app_ch(d, cap, n, tmp[--i]);
    }
}

static inline void sh_app_i(char *d, int cap, int *n, int v) {
    if (v < 0) {
        sh_app_ch(d, cap, n, '-');
        sh_app_u(d, cap, n, (unsigned)(-(v + 1)) + 1u);
        return;
    }
    sh_app_u(d, cap, n, (unsigned)v);
}

static inline void sh_app_f(char *d, int cap, int *n, float v) {
    int neg = 0;
    int ip;
    int k;
    float frac;
    if (v != v) {
        sh_app(d, cap, n, "0.0");
        return;
    }
    if (v < 0.f) {
        neg = 1;
        v = -v;
    }
    if (v > 1000000.f) {
        v = 1000000.f;
    }
    if (neg) {
        sh_app_ch(d, cap, n, '-');
    }
    ip = (int)v;
    frac = v - (float)ip;
    sh_app_i(d, cap, n, ip);
    sh_app_ch(d, cap, n, '.');
    for (k = 0; k < 6; ++k) {
        int digit;
        frac *= 10.f;
        digit = (int)frac;
        if (digit < 0) {
            digit = 0;
        }
        if (digit > 9) {
            digit = 9;
        }
        sh_app_ch(d, cap, n, (char)('0' + digit));
        frac -= (float)digit;
    }
}

static inline int sh_eq(const char *a, const char *b) {
    if (!a || !b) {
        return 0;
    }
    while (*a && *a == *b) {
        ++a;
        ++b;
    }
    return *a == 0 && *b == 0;
}

static inline int sh_eqn(const char *s, int n, const char *lit) {
    int i = 0;
    if (!s || !lit || n < 0) {
        return 0;
    }
    while (i < n && lit[i] && s[i] == lit[i]) {
        ++i;
    }
    return i == n && lit[i] == 0;
}

static inline int sh_ncomp_ty(int ty) {
    switch (ty) {
    case SH_TY_BOOL:
    case SH_TY_INT:
    case SH_TY_FLOAT:
        return 1;
    case SH_TY_VEC2:
    case SH_TY_IVEC2:
        return 2;
    case SH_TY_VEC3:
    case SH_TY_IVEC3:
        return 3;
    case SH_TY_VEC4:
    case SH_TY_IVEC4:
        return 4;
    case SH_TY_MAT3:
        return 3;
    case SH_TY_MAT4:
        return 4;
    default:
        return 0;
    }
}

static inline int sh_is_vec(int ty) {
    return ty >= SH_TY_VEC2 && ty <= SH_TY_VEC4;
}

static inline int sh_is_scalar(int ty) {
    return ty == SH_TY_BOOL || ty == SH_TY_INT || ty == SH_TY_FLOAT;
}

static inline int sh_is_mat(int ty) {
    return ty == SH_TY_MAT3 || ty == SH_TY_MAT4;
}

static inline const char *sh_ty_name(int ty) {
    switch (ty) {
    case SH_TY_VOID:
        return "void";
    case SH_TY_BOOL:
        return "bool";
    case SH_TY_INT:
        return "int";
    case SH_TY_FLOAT:
        return "float";
    case SH_TY_VEC2:
        return "vec2";
    case SH_TY_VEC3:
        return "vec3";
    case SH_TY_VEC4:
        return "vec4";
    case SH_TY_IVEC2:
        return "ivec2";
    case SH_TY_IVEC3:
        return "ivec3";
    case SH_TY_IVEC4:
        return "ivec4";
    case SH_TY_MAT3:
        return "mat3";
    case SH_TY_MAT4:
        return "mat4";
    case SH_TY_SAMPLER2D:
        return "sampler2D";
    default:
        return "unknown";
    }
}

void sh_err(ShComp *c, int tok, const char *msg);
void sh_err_tok(ShComp *c, int kind_tok_index, const char *msg);
int sh_lex(ShComp *c);
int sh_parse(ShComp *c);
int sh_sem(ShComp *c);
int sh_opt(ShComp *c);
int sh_verify(ShComp *c);
int sh_emit_tgsi(ShComp *c, char *dst, int cap, const int *var_remap);
int sh_dump_ir_buf(const ShComp *c, char *dst, int cap);
int sh_dump_ast_buf(const ShComp *c, char *dst, int cap);
int sh_exec_ir(const ShComp *c, const float *attr, const float *uni, const float *var_in,
               const float fragcoord[4], const uint8_t *tex, int tex_w, int tex_h,
               float *pos, float vary_out[][4], float color[4], int *discarded,
               const int *var_remap);

#endif
