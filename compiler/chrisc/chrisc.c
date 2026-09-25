#include "chrisc.h"
#include "../clvm/clvm.h"
#include "../clvm/clvm_vm.h"
#include "../clvm/clasm.h"

#define TOK_MAX 262144
#define NODE_MAX 131072
#define ARG_MAX 65536
#define SYM_MAX 16384
#define NAME_MAX 48
#define FUNC_MAX 4096
#define STRUCT_MAX 1024
#define FIELD_MAX 96
#define FUNC_ARG_MAX 16
#define CALL_ARG_LOCAL 32
#define FNPTR_MAX 1024
#define GINIT_MAX 65536
#define CALL_PATCH_MAX 8192
#define INCLUDE_MAX 1024
#define INCLUDE_DEPTH 16
#define FILE_NAME_MAX 128
#define MAP_MAX 8192
#define STR_POOL_MAX 131072
#define LOOP_MAX 16
#define LOOP_PATCH_MAX 256
#define DEF_MAX 4096
#define DEF_BODY 8192
#define DEF_PARAM_MAX 16
#define DEF_PARAM_LEN 24
#define ENUM_MAX 4096
#define TYPEDEF_MAX 1024
#define LABEL_MAX 1024
#define SWITCH_CASE_MAX 256
#define PRAGMA_ONCE_MAX 256
#define HT_DEF_N 8192
#define HT_FUNC_N 8192
#define HT_TD_N 2048
#define HT_ST_N 2048
#define HT_SYM_N 32768
#define HT_CONST_N 8192
#define INC_CACHE_N 160
#define INC_CACHE_STORE (2u * 1024u * 1024u)

typedef enum TokenKind {
    T_EOF = 0, T_ID, T_NUM, T_FNUM, T_STR, T_VOID, T_INT, T_FLOAT, T_CHAR,
    T_STRUCT, T_IF, T_ELSE, T_WHILE, T_FOR, T_RETURN, T_BREAK, T_CONTINUE,
    T_LP, T_RP, T_LB, T_RB, T_LBRACK, T_RBRACK, T_SEMI, T_COMMA, T_ASSIGN,
    T_PLUS, T_MINUS, T_DOT,
    T_STAR, T_SLASH, T_PERCENT, T_EQ, T_NE, T_LT, T_LE, T_GT, T_GE,
    T_ANDAND, T_OROR, T_NOT,
    T_LONG, T_SHORT, T_UNSIGNED, T_SIGNED, T_CONST, T_STATIC, T_EXTERN,
    T_VOLATILE, T_ENUM, T_UNION, T_TYPEDEF, T_SWITCH, T_CASE, T_DEFAULT,
    T_DO, T_GOTO, T_SIZEOF, T_ASM, T_AMP, T_PIPE, T_CARET, T_TILDE,
    T_LSH, T_RSH, T_PLUSPLUS, T_MINUSMINUS, T_PLUSEQ, T_MINUSEQ, T_STAREQ,
    T_SLASHEQ, T_AMPEQ, T_PIPEEQ, T_CARETEQ, T_PERCENTEQ, T_LSHEQ, T_RSHEQ,
    T_QUESTION, T_COLON, T_ELLIPSIS, T_ARROW, T_GENERIC,
    T_STATIC_ASSERT, T_ALIGNAS, T_ALIGNOF, T_BOOL, T_INLINE, T_RESTRICT,
    T_NORETURN, T_THREADLOCAL, T_COMPLEX, T_IF0
} TokenKind;

typedef struct Token {
    TokenKind kind;
    int32_t value;
    int line, column;
    char name[NAME_MAX];
} Token;

typedef enum NodeKind {
    N_BLOCK, N_DECL, N_ASSIGN, N_INDEX_ASSIGN, N_IF, N_WHILE, N_FOR, N_RETURN,
    N_EXPR, N_FIELD, N_FIELD_ASSIGN, N_BREAK, N_CONTINUE,
    N_INT, N_FLOAT, N_STR, N_VAR, N_INDEX, N_CALL, N_NEG, N_NOT, N_ADD, N_SUB,
    N_MUL, N_DIV, N_MOD,
    N_EQ, N_NE, N_LT, N_LE, N_GT, N_GE, N_AND, N_OR,
    N_BITAND, N_BITOR, N_BITXOR, N_SHL, N_SHR, N_BITNOT,
    N_ADDR, N_DEREF, N_SIZEOF, N_CAST, N_TERNARY, N_COMMA,
    N_PREINC, N_PREDEC, N_POSTINC, N_POSTDEC, N_ARROW,
    N_SWITCH, N_CASE, N_DEFAULT, N_DO, N_GOTO, N_LABEL, N_ASM, N_FNPTR,
    N_DEREF_ASSIGN, N_PTRFIELD
} NodeKind;

typedef struct Node {
    NodeKind kind;
    int left, right, third, next;
    int32_t value;
    int line, column;
    uint8_t is_float;
    int16_t sid;
    char name[NAME_MAX];
} Node;

#define CAST_W_MASK 0xFFu
#define CAST_UNS    0x100
#define CAST_PTR    0x200
#define CAST_BOOL   0x400
#define CAST_VOID   0x800
#define CAST_PFL    0x1000000
#define CAST_WIDTH(v)  ((int)((uint32_t)(v) & CAST_W_MASK))
#define CAST_POINTEE(v) ((int)(((uint32_t)(v) >> 16) & 0xFFu))

typedef struct CastType {
    uint8_t width;
    uint8_t is_float;
    uint8_t is_uns;
    uint8_t is_ptr;
    uint8_t is_void;
    uint8_t is_bool;
    uint8_t pointee;
    uint8_t pointee_float;
    int16_t sid;
} CastType;

typedef struct Symbol {
    char name[NAME_MAX];
    uint32_t address;
    uint8_t is_float;
    uint8_t packed;
    uint8_t is_array;
    uint8_t is_ptr;
    uint8_t is_unsigned;
    uint8_t is_global;
    uint16_t scope;
    uint8_t width;
    uint8_t is_const;
    uint8_t is_complex;
    uint16_t pointee;
    int16_t struct_id;
    uint16_t stride;
    uint16_t array_n;
    uint8_t is_tls;
    uint16_t tls_i;
} Symbol;

typedef struct StructDef {
    char name[NAME_MAX];
    int nfields;
    char fname[FIELD_MAX][NAME_MAX];
    uint8_t is_float[FIELD_MAX];
    uint8_t fptr[FIELD_MAX];
    uint16_t fwidth[FIELD_MAX];
    uint16_t felemw[FIELD_MAX]; /* 0 = scalar/use fwidth; else array elem width */
    uint16_t foff[FIELD_MAX];
    int16_t fstruct[FIELD_MAX];
    uint8_t fbits[FIELD_MAX];
    uint8_t fbitoff[FIELD_MAX];
    uint16_t size;
    uint8_t packed;
    uint8_t is_union;
} StructDef;

typedef struct DeclType {
    uint16_t w;
    uint8_t ptr;
    uint8_t isf;
    uint8_t uns;
    uint8_t is_void;
    int16_t sid;
    uint16_t arr_n;  /* >1: typedef/type is an array of arr_n elements */
    uint16_t elemw;  /* array element width when arr_n > 1 */
} DeclType;

typedef struct FuncDef {
    char name[NAME_MAX];
    uint8_t argc;
    uint8_t ret;
    uint8_t arg_float[FUNC_ARG_MAX];
    uint8_t arg_width[FUNC_ARG_MAX];
    uint32_t arg_addr[FUNC_ARG_MAX];
    int body;
    int entry;
    uint8_t is_main;
    uint8_t is_varargs;
    int16_t ret_sid;
} FuncDef;

typedef struct CallPatch {
    int at;
    int fn;
} CallPatch;

typedef struct LineMap {
    int unit_line;
    int file_id;
    int orig_line;
} LineMap;

typedef struct Builtin {
    const char *name;
    uint16_t id;
    uint8_t argc, returns, ret_float;
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
    int nfnptr;
    int fnptr_at[FNPTR_MAX];
    int fnptr_fn[FNPTR_MAX];
    int nginits;
    uint32_t ginit_addr[GINIT_MAX];
    uint8_t ginit_w[GINIT_MAX];
    int ginit_expr[GINIT_MAX];
    uint32_t va_base;
    uint32_t icall_base;
    int cur_fn;
    int scope_base;
    uint32_t mem_next;
    uint8_t *out;
    size_t cap, pc;
    ChrisResult *result;
    ChriscReadFn read_fn;
    void *read_user;
    char files[INCLUDE_MAX][FILE_NAME_MAX];
    int nfiles;
    LineMap map[MAP_MAX];
    int nmap;
    char str_pool[STR_POOL_MAX];
    int str_len;
    uint32_t str_base;
    int loop_sp;
    int loop_brk[LOOP_MAX][LOOP_PATCH_MAX];
    int loop_nbrk[LOOP_MAX];
    int loop_cont[LOOP_MAX][LOOP_PATCH_MAX];
    int loop_ncont[LOOP_MAX];
    uint8_t loop_switch[LOOP_MAX];
    int pp_skip;
    int pp_depth;
    int pp_true[16];
    int pp_taken[16];
    int ndef;
    char def_name[DEF_MAX][NAME_MAX];
    char def_body[DEF_MAX][DEF_BODY];
    int def_fn[DEF_MAX];
    int def_nparam[DEF_MAX];
    char def_param[DEF_MAX][DEF_PARAM_MAX][DEF_PARAM_LEN];
    int nlabels;
    char label_name[LABEL_MAX][NAME_MAX];
    int label_pc[LABEL_MAX];
    int ngoto;
    int goto_at[LABEL_MAX];
    char goto_name[LABEL_MAX][NAME_MAX];
    int ntypedef;
    char td_name[TYPEDEF_MAX][NAME_MAX];
    int td_width[TYPEDEF_MAX];
    int td_ptr[TYPEDEF_MAX];
    int td_struct[TYPEDEF_MAX];
    int td_count[TYPEDEF_MAX];
    int td_elemw[TYPEDEF_MAX];
    int nconst;
    char const_name[ENUM_MAX][NAME_MAX];
    int32_t const_val[ENUM_MAX];
    int pragma_once_n;
    char pragma_once[PRAGMA_ONCE_MAX][FILE_NAME_MAX];
    int scope_sp;
    int scope_seq;
    int scope_stack[32];
    int tu_id;
    uint8_t saw_static;
    uint8_t saw_const;
    int align_next;
    int field_bits;
    uint8_t saw_tls;
    int n_tls;
    uint8_t pack_pragma;
    uint16_t abi_major;
    uint16_t abi_minor;
    int pp_line;
    int pp_file;
    uint16_t ht_def[HT_DEF_N];
    uint16_t ht_func[HT_FUNC_N];
    uint16_t ht_td[HT_TD_N];
    uint16_t ht_st[HT_ST_N];
    uint16_t ht_sym[HT_SYM_N];
    uint16_t ht_const[HT_CONST_N];
} Compiler;

static Compiler g_chrisc;
static char g_unit[CHRIS_SOURCE_MAX];
static char g_tu[CHRIS_SOURCE_MAX];
static char g_inc[INCLUDE_DEPTH][CHRIS_INC_MAX];
static char g_line_exp[16384];
static char g_logical[32768];
static void (*g_yield_fn)(void);
static int g_yield_ctr;
static char g_ic_path[INC_CACHE_N][FILE_NAME_MAX];
static uint32_t g_ic_off[INC_CACHE_N];
static int g_ic_len[INC_CACHE_N];
static int g_ic_n;
static char g_ic_store[INC_CACHE_STORE];
static uint32_t g_ic_used;

static int same(const char *a, const char *b);
static void text(char *d, size_t n, const char *s);

void chrisc_set_yield(void (*fn)(void)) {
    g_yield_fn = fn;
    g_yield_ctr = 0;
}

static void maybe_yield(void) {
    g_yield_ctr++;
    if (g_yield_fn && (g_yield_ctr & 1023) == 0) {
        g_yield_fn();
    }
}

static unsigned hash_str(const char *s) {
    unsigned h = 2166136261u;
    if (!s) {
        return 0;
    }
    while (*s) {
        h ^= (unsigned char)*s++;
        h *= 16777619u;
    }
    return h;
}

static void ht_zero(uint16_t *tab, int n) {
    int i;
    for (i = 0; i < n; ++i) {
        tab[i] = 0;
    }
}

static int ht_find_grid(const uint16_t *tab, int cap, const char *key,
                        const char *grid, int stride) {
    unsigned h = hash_str(key);
    int i;
    if (!key) {
        return -1;
    }
    for (i = 0; i < cap; ++i) {
        unsigned s = (h + (unsigned)i) & (unsigned)(cap - 1);
        uint16_t v = tab[s];
        if (!v) {
            return -1;
        }
        if (same(grid + (int)(v - 1) * stride, key)) {
            return (int)(v - 1);
        }
    }
    return -1;
}

static void ht_ins_grid(uint16_t *tab, int cap, const char *key, int id,
                        const char *grid, int stride) {
    unsigned h = hash_str(key);
    int i;
    if (!key || id < 0) {
        return;
    }
    for (i = 0; i < cap; ++i) {
        unsigned s = (h + (unsigned)i) & (unsigned)(cap - 1);
        uint16_t v = tab[s];
        if (!v) {
            tab[s] = (uint16_t)(id + 1);
            return;
        }
        if (same(grid + (int)(v - 1) * stride, key)) {
            return;
        }
    }
}

static void lookups_reset(Compiler *c) {
    ht_zero(c->ht_def, HT_DEF_N);
    ht_zero(c->ht_func, HT_FUNC_N);
    ht_zero(c->ht_td, HT_TD_N);
    ht_zero(c->ht_st, HT_ST_N);
    ht_zero(c->ht_sym, HT_SYM_N);
    ht_zero(c->ht_const, HT_CONST_N);
}

static void inc_cache_reset(void) {
    g_ic_n = 0;
    g_ic_used = 0;
}

static int inc_cache_get(const char *path, char *out, int cap) {
    int i;
    for (i = 0; i < g_ic_n; ++i) {
        if (same(g_ic_path[i], path)) {
            int n = g_ic_len[i];
            int k;
            if (n >= cap) {
                n = cap - 1;
            }
            for (k = 0; k < n; ++k) {
                out[k] = g_ic_store[g_ic_off[i] + (uint32_t)k];
            }
            out[n] = 0;
            return n;
        }
    }
    return -1;
}

static void inc_cache_put(const char *path, const char *src, int n) {
    uint32_t off;
    int k;
    if (!path || n < 0 || g_ic_n >= INC_CACHE_N) {
        return;
    }
    if (g_ic_used + (uint32_t)n + 1u > INC_CACHE_STORE) {
        return;
    }
    off = g_ic_used;
    for (k = 0; k < n; ++k) {
        g_ic_store[off + (uint32_t)k] = src[k];
    }
    g_ic_store[off + (uint32_t)n] = 0;
    g_ic_used += (uint32_t)n + 1u;
    text(g_ic_path[g_ic_n], FILE_NAME_MAX, path);
    g_ic_off[g_ic_n] = off;
    g_ic_len[g_ic_n] = n;
    g_ic_n++;
}

static int read_include(Compiler *c, const char *path, char *out, int cap) {
    int n;
    n = inc_cache_get(path, out, cap);
    if (n >= 0) {
        return n;
    }
    if (!c->read_fn || !path) {
        return -1;
    }
    n = c->read_fn(c->read_user, path, out, cap);
    if (n >= 0) {
        inc_cache_put(path, out, n);
    }
    return n;
}

static const Builtin builtins[] = {
    {"pixel", 1, 3, 0, 0}, {"rect", 2, 5, 0, 0}, {"line", 3, 5, 0, 0},
    {"sprite", 4, 6, 0, 0}, {"tilemap", 5, 7, 0, 0}, {"clear", 6, 1, 0, 0},
    {"key", 10, 1, 1, 0}, {"ticks", 11, 0, 1, 0}, {"wait", 12, 1, 0, 0},
    {"tone", 13, 2, 0, 0}, {"tri", 20, 10, 0, 0}, {"mesh", 21, 5, 0, 0},
    {"sbrk", 180, 1, 1, 0},
    {"scene_add", 190, 6, 1, 0},
    {"scene_draw", 191, 3, 1, 0},
    {"phys_add", 192, 4, 1, 0},
    {"phys_step", 193, 0, 1, 0},
    {"phys_x", 196, 1, 1, 0}, {"phys_y", 197, 1, 1, 0},
    {"tex_ofs", 198, 2, 0, 0},
    {"anim_key", 194, 5, 1, 0},
    {"anim_apply", 195, 2, 1, 0},
    {"transform", 22, 3, 0, 0}, {"meshf", 23, 8, 0, 0},
    {"sin", 31, 1, 1, 1}, {"cos", 32, 1, 1, 1},
    {"cam", 33, 5, 0, 0}, {"light", 34, 6, 0, 0}, {"tex", 35, 1, 0, 0},
    {"voxel", 36, 4, 0, 0}, {"voxel_get", 37, 3, 1, 0}, {"world", 38, 0, 0, 0},
    {"viewport", 39, 2, 0, 0}, {"screen_w", 40, 0, 1, 0}, {"screen_h", 41, 0, 1, 0},
    {"fps", 30, 0, 1, 0},
    {"fopen", 50, 1, 1, 0}, {"fclose", 51, 1, 0, 0},
    {"fread", 52, 3, 1, 0}, {"fwrite", 53, 3, 1, 0},
    {"fsize", 54, 1, 1, 0}, {"fexists", 55, 1, 1, 0},
    {"malloc", 56, 1, 1, 0}, {"free", 57, 1, 0, 0},
    {"realloc", 61, 2, 1, 0},
    {"setjmp", 58, 1, 1, 0}, {"longjmp", 59, 2, 0, 0},
    {"gc_alloc", 70, 1, 1, 0}, {"gc_collect", 71, 0, 0, 0},
    {"thrd_create", 62, 2, 1, 0}, {"thrd_join", 63, 1, 1, 0},
    {"mtx_lock", 127, 1, 1, 0}, {"mtx_unlock", 128, 1, 1, 0},
    {"cnd_wait", 129, 2, 1, 0}, {"cnd_signal", 130, 1, 1, 0},
    {"tls_get", 131, 1, 1, 0}, {"tls_set", 132, 2, 0, 0},
    {"sock_listen", 140, 1, 1, 0}, {"sock_accept", 141, 1, 1, 0},
    {"sock_connect", 142, 2, 1, 0}, {"sock_send", 143, 3, 1, 0},
    {"sock_recv", 144, 3, 1, 0}, {"sock_close", 145, 1, 1, 0},
    {"dns_lookup", 146, 1, 1, 0},
    {"app_state", 133, 2, 1, 0}, {"app_reload", 134, 0, 1, 0},
    {"rng_u32", 150, 0, 1, 0},
    {"sha256", 151, 3, 1, 0}, {"aes_encrypt", 152, 3, 1, 0},
    {"x25519", 153, 3, 1, 0},
    {"pcm_write", 160, 2, 1, 0},
    {"drv_outw", 170, 2, 1, 0}, {"drv_inw", 171, 1, 1, 0},
    {"drv_outb", 172, 2, 1, 0}, {"drv_inb", 173, 1, 1, 0},
    {"drv_irq", 174, 1, 1, 0}, {"drv_pci", 175, 4, 1, 0},
    {"pci_write", 210, 5, 1, 0}, {"bar_map", 211, 4, 1, 0},
    {"mmio_r32", 212, 2, 1, 0}, {"mmio_w32", 213, 3, 1, 0},
    {"mmio_r8", 214, 2, 1, 0}, {"mmio_w8", 215, 3, 1, 0},
    {"mmio_r16", 216, 2, 1, 0}, {"mmio_w16", 217, 3, 1, 0},
    {"dma_alloc", 218, 1, 1, 0}, {"dma_lo", 219, 1, 1, 0},
    {"dma_hi", 220, 1, 1, 0}, {"dma_w32", 221, 3, 1, 0},
    {"dma_r32", 222, 2, 1, 0}, {"disk_sectors", 223, 0, 1, 0},
    {"disk_read", 224, 3, 1, 0}, {"disk_write", 225, 3, 1, 0},
    {"disk_format", 226, 0, 1, 0}, {"gpu_arm", 227, 7, 1, 0},
    {"gpu_ready", 228, 0, 1, 0},
    {"disk_count", 229, 0, 1, 0}, {"disk_install", 230, 1, 1, 0},
    {"disk_kind", 231, 1, 1, 0}, {"disk_flags", 232, 1, 1, 0},
    {"disk_sectors", 233, 1, 1, 0},
    {"cla_load", 64, 1, 1, 0},
    {"fseek", 65, 2, 1, 0},
    {"fb_blit", 72, 3, 0, 0}, {"setpal", 73, 1, 0, 0},
    {"mouse_x", 80, 0, 1, 0}, {"mouse_y", 81, 0, 1, 0},
    {"mouse_btn", 82, 0, 1, 0}, {"ev_key", 83, 0, 1, 0},
    {"mouse_dx", 240, 0, 1, 0}, {"mouse_dy", 241, 0, 1, 0},
    {"mouse_cap", 242, 0, 1, 0}, {"mouse_rel", 243, 0, 1, 0},
    {"ev_text", 84, 0, 1, 0}, {"fillrgb", 85, 5, 0, 0},
    {"text", 86, 4, 0, 0}, {"glyph", 87, 4, 0, 0},
    {"surf_place", 88, 4, 0, 0}, {"surf_move", 89, 2, 0, 0},
    {"surf_raise", 90, 0, 0, 0}, {"surf_close", 91, 0, 0, 0},
    {"readdir", 92, 3, 1, 0}, {"mkdir", 93, 1, 1, 0},
    {"unlink", 94, 1, 1, 0}, {"rename", 95, 2, 1, 0},
    {"app_spawn", 96, 1, 1, 0}, {"app_kill", 97, 1, 1, 0},
    {"app_count", 98, 0, 1, 0}, {"app_info", 99, 2, 1, 0},
    {"sys_cc", 100, 1, 1, 0}, {"sys_run", 101, 1, 1, 0},
    {"sys_make", 102, 2, 1, 0}, {"disp_w", 103, 0, 1, 0},
    {"disp_h", 104, 0, 1, 0}, {"sys_err", 105, 1, 1, 0},
    {"app_spawn_arg", 106, 2, 1, 0}, {"app_arg", 107, 1, 1, 0},
    {"lib_load", 108, 1, 1, 0}, {"lib_reload", 109, 1, 1, 0},
    {"isdir", 110, 1, 1, 0},
    {"kb_layout", 111, 1, 1, 0}, {"kb_get", 112, 0, 1, 0},
    {"surf_resize", 113, 2, 0, 0}, {"surf_minimize", 114, 0, 0, 0},
    {"surf_maximize", 115, 4, 0, 0}, {"surf_restore", 116, 4, 0, 0},
    {"sys_heap_used_kb", 117, 0, 1, 0},
    {"sys_heap_free_kb", 118, 0, 1, 0},
    {"sys_pmm_free_pages", 119, 0, 1, 0},
    {"sys_frame_p50", 120, 0, 1, 0},
    {"sys_frame_p95", 121, 0, 1, 0},
    {"sys_cfs_hits", 122, 0, 1, 0},
    {"sys_cfs_misses", 123, 0, 1, 0},
    {"sys_active_apps", 124, 0, 1, 0},
    {"app_raise", 125, 1, 1, 0},
    {"textruns", 126, 2, 0, 0},
    {"dbg_ctl", 250, 2, 1, 0}, {"dbg_text", 251, 2, 1, 0},
    {"shader_make", 260, 2, 1, 0}, {"shader_ok", 261, 1, 1, 0},
    {"shader_log", 262, 2, 1, 0}, {"shader_drop", 263, 1, 1, 0},
    {"prog_make", 264, 0, 1, 0}, {"prog_attach", 265, 2, 1, 0},
    {"prog_link", 266, 1, 1, 0}, {"prog_ok", 267, 1, 1, 0},
    {"prog_log", 268, 2, 1, 0}, {"prog_drop", 269, 1, 1, 0},
    {"prog_uniloc", 270, 2, 1, 0}, {"prog_setf", 271, 4, 1, 0},
    {"prog_samp", 272, 2, 1, 0}, {"shader_vert", 273, 0, 1, 0},
    {"shader_frag", 274, 0, 1, 0}
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

static void line_origin(Compiler *c, int line, int *file_id, int *orig) {
    int i;
    int fid = 0;
    int o = line;
    for (i = 0; i < c->nmap; ++i) {
        if (c->map[i].unit_line <= line) {
            fid = c->map[i].file_id;
            o = c->map[i].orig_line + (line - c->map[i].unit_line);
        }
    }
    if (file_id) {
        *file_id = fid;
    }
    if (orig) {
        *orig = o;
    }
}

int chrisc_diag_push(ChrisResult *result, int severity, int code,
                     const char *file, int line, int column, int end_line,
                     int end_column, const char *message) {
    ChrisDiagnostic *d;
    if (!result || result->diag_n < 0 || result->diag_n >= CHRIS_DIAG_MAX) {
        return 0;
    }
    d = &result->diags[result->diag_n++];
    d->severity = severity;
    d->code = code;
    d->line = line;
    d->column = column;
    d->end_line = end_line;
    d->end_column = end_column;
    text(d->file, sizeof(d->file), file ? file : "");
    text(d->message, sizeof(d->message), message ? message : "");
    return 1;
}

static void clear_diag(ChrisResult *result) {
    result->diag.line = result->diag.column = 0;
    result->diag.file[0] = 0;
    result->diag.message[0] = 0;
    result->diag_n = 0;
    result->nfiles = 0;
}

static void set_diag(ChrisResult *result, int line, int col, const char *file,
                     const char *message) {
    result->diag.line = line;
    result->diag.column = col;
    text(result->diag.file, sizeof(result->diag.file), file ? file : "");
    text(result->diag.message, sizeof(result->diag.message),
         message ? message : "");
    chrisc_diag_push(result, CHRIS_SEV_ERROR, 0, file, line, col, line, col,
                     message);
}

static int fail(Compiler *c, int line, int col, const char *m) {
    int file_id = 0;
    int orig = line;
    const char *file = "";
    line_origin(c, line, &file_id, &orig);
    if (file_id >= 0 && file_id < c->nfiles) {
        file = c->files[file_id];
    }
    set_diag(c->result, orig, col, file, m);
    return 0;
}

static TokenKind keyword(const char *s) {
    if (same(s, "void")) {
        return T_VOID;
    }
    if (same(s, "int")) {
        return T_INT;
    }
    if (same(s, "float") || same(s, "double")) {
        return T_FLOAT;
    }
    if (same(s, "char")) {
        return T_CHAR;
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
    if (same(s, "break")) {
        return T_BREAK;
    }
    if (same(s, "continue")) {
        return T_CONTINUE;
    }
    if (same(s, "long")) {
        return T_LONG;
    }
    if (same(s, "short")) {
        return T_SHORT;
    }
    if (same(s, "unsigned")) {
        return T_UNSIGNED;
    }
    if (same(s, "signed")) {
        return T_SIGNED;
    }
    if (same(s, "const")) {
        return T_CONST;
    }
    if (same(s, "static")) {
        return T_STATIC;
    }
    if (same(s, "extern")) {
        return T_EXTERN;
    }
    if (same(s, "volatile")) {
        return T_VOLATILE;
    }
    if (same(s, "enum")) {
        return T_ENUM;
    }
    if (same(s, "union")) {
        return T_UNION;
    }
    if (same(s, "typedef")) {
        return T_TYPEDEF;
    }
    if (same(s, "switch")) {
        return T_SWITCH;
    }
    if (same(s, "case")) {
        return T_CASE;
    }
    if (same(s, "default")) {
        return T_DEFAULT;
    }
    if (same(s, "do")) {
        return T_DO;
    }
    if (same(s, "goto")) {
        return T_GOTO;
    }
    if (same(s, "sizeof")) {
        return T_SIZEOF;
    }
    if (same(s, "asm")) {
        return T_ASM;
    }
    if (same(s, "_Generic")) {
        return T_GENERIC;
    }
    if (same(s, "_Static_assert")) {
        return T_STATIC_ASSERT;
    }
    if (same(s, "_Alignas")) {
        return T_ALIGNAS;
    }
    if (same(s, "_Alignof")) {
        return T_ALIGNOF;
    }
    if (same(s, "_Bool") || same(s, "bool")) {
        return T_BOOL;
    }
    if (same(s, "register") || same(s, "auto")) {
        return T_INLINE;
    }
    if (same(s, "inline") || same(s, "_Inline")) {
        return T_INLINE;
    }
    if (same(s, "restrict")) {
        return T_RESTRICT;
    }
    if (same(s, "_Noreturn")) {
        return T_NORETURN;
    }
    if (same(s, "_Thread_local") || same(s, "thread_local")) {
        return T_THREADLOCAL;
    }
    if (same(s, "_Complex")) {
        return T_COMPLEX;
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

static int has_slash(const char *s) {
    int i;
    for (i = 0; s[i]; ++i) {
        if (s[i] == '/') {
            return 1;
        }
    }
    return 0;
}

static int path_dir(const char *path, char *out, int cap) {
    int last = -1;
    int i;
    for (i = 0; path && path[i]; ++i) {
        if (path[i] == '/') {
            last = i;
        }
    }
    if (last < 0) {
        if (cap > 0) {
            out[0] = 0;
        }
        return 1;
    }
    if (last >= cap) {
        last = cap - 1;
    }
    for (i = 0; i < last; ++i) {
        out[i] = path[i];
    }
    out[i] = 0;
    return 1;
}

static int resolve_include(const char *cur_path, const char *inc, char *out,
                           int cap) {
    char dir[FILE_NAME_MAX];
    int i = 0;
    int j = 0;
    if (!inc || !inc[0]) {
        return 0;
    }
    if (has_slash(inc) || !cur_path || !cur_path[0]) {
        text(out, (size_t)cap, inc);
        return 1;
    }
    path_dir(cur_path, dir, FILE_NAME_MAX);
    if (!dir[0]) {
        text(out, (size_t)cap, inc);
        return 1;
    }
    while (dir[i] && i + 1 < cap) {
        out[i] = dir[i];
        ++i;
    }
    if (i + 1 < cap) {
        out[i++] = '/';
    }
    while (inc[j] && i + 1 < cap) {
        out[i++] = inc[j++];
    }
    out[i] = 0;
    return 1;
}

static int file_id_add(Compiler *c, const char *path) {
    int i;
    for (i = 0; i < c->nfiles; ++i) {
        if (same(c->files[i], path)) {
            return i;
        }
    }
    if (c->nfiles == INCLUDE_MAX) {
        return -1;
    }
    i = c->nfiles++;
    text(c->files[i], FILE_NAME_MAX, path ? path : "");
    return i;
}

static int map_push(Compiler *c, int unit_line, int file_id, int orig_line) {
    if (c->nmap == MAP_MAX) {
        return 0;
    }
    c->map[c->nmap].unit_line = unit_line;
    c->map[c->nmap].file_id = file_id;
    c->map[c->nmap].orig_line = orig_line;
    c->nmap++;
    return 1;
}

static int unit_put(size_t *ulen, int ch) {
    if (*ulen + 1 >= CHRIS_SOURCE_MAX) {
        return 0;
    }
    g_unit[(*ulen)++] = (char)ch;
    return 1;
}

static int expand_file(Compiler *c, const char *path, const char *src, size_t n,
                       size_t *ulen, int *unit_line, int depth);
static int sys_inc_map(const char *inc, char *out, int cap);
static size_t compact_line_cont(char *s, size_t n);
static int once_has(Compiler *c, const char *path);

static int expand_include(Compiler *c, const char *cur_path, const char *inc,
                          size_t *ulen, int *unit_line, int depth) {
    char resolved[FILE_NAME_MAX];
    int n;
    if (depth >= INCLUDE_DEPTH) {
        return fail(c, *unit_line, 1, "include nested too deep");
    }
    if (!c->read_fn) {
        return fail(c, *unit_line, 1, "include needs filesystem");
    }
    if (!resolve_include(cur_path, inc, resolved, FILE_NAME_MAX)) {
        return fail(c, *unit_line, 1, "bad include path");
    }
    if (once_has(c, resolved)) {
        return 1;
    }
    n = read_include(c, resolved, g_inc[depth], (int)CHRIS_INC_MAX - 1);
    if (n < 0) {
        char mapped[FILE_NAME_MAX];
        if (sys_inc_map(inc, mapped, FILE_NAME_MAX)) {
            text(resolved, FILE_NAME_MAX, mapped);
            if (once_has(c, resolved)) {
                return 1;
            }
            n = read_include(c, resolved, g_inc[depth], (int)CHRIS_INC_MAX - 1);
        }
    }
    if (n < 0 && !has_slash(inc)) {
        resolved[0] = 'L';
        resolved[1] = 'I';
        resolved[2] = 'B';
        resolved[3] = '/';
        {
            int k = 0;
            while (inc[k] && 4 + k + 1 < FILE_NAME_MAX) {
                resolved[4 + k] = inc[k];
                k++;
            }
            resolved[4 + k] = 0;
        }
        if (once_has(c, resolved)) {
            return 1;
        }
        n = read_include(c, resolved, g_inc[depth], (int)CHRIS_INC_MAX - 1);
    }
    if (n < 0) {
        return fail(c, *unit_line, 1, "include not found");
    }
    g_inc[depth][n] = 0;
    n = (int)compact_line_cont(g_inc[depth], (size_t)n);
    return expand_file(c, resolved, g_inc[depth], (size_t)n, ulen, unit_line,
                       depth + 1);
}

static int line_is_include(const char *line, size_t n, char *inc, int cap) {
    size_t i = 0;
    size_t k;
    while (i < n && (line[i] == ' ' || line[i] == '\t')) {
        ++i;
    }
    if (i >= n || line[i] != '#') {
        return 0;
    }
    ++i;
    while (i < n && (line[i] == ' ' || line[i] == '\t')) {
        ++i;
    }
    if (i + 7 > n) {
        return 0;
    }
    if (line[i] != 'i' || line[i + 1] != 'n' || line[i + 2] != 'c' ||
        line[i + 3] != 'l' || line[i + 4] != 'u' || line[i + 5] != 'd' ||
        line[i + 6] != 'e') {
        return 0;
    }
    i += 7;
    while (i < n && (line[i] == ' ' || line[i] == '\t')) {
        ++i;
    }
    if (i >= n || (line[i] != '"' && line[i] != '<')) {
        return 0;
    }
    {
        int end = line[i] == '<' ? '>' : '"';
        ++i;
        k = 0;
        while (i < n && line[i] != end && line[i] != '\n') {
            if (k + 1 < (size_t)cap) {
                inc[k++] = line[i];
            }
            ++i;
        }
        inc[k] = 0;
        return line[i] == end;
    }
}

static int line_starts_hash(const char *line, size_t n, size_t *kw) {
    size_t i = 0;
    while (i < n && (line[i] == ' ' || line[i] == '\t')) {
        ++i;
    }
    if (i >= n || line[i] != '#') {
        return 0;
    }
    ++i;
    while (i < n && (line[i] == ' ' || line[i] == '\t')) {
        ++i;
    }
    *kw = i;
    return 1;
}

static int kw_is(const char *line, size_t n, size_t i, const char *w) {
    size_t k = 0;
    while (w[k] && i + k < n && line[i + k] == w[k]) {
        ++k;
    }
    return w[k] == 0 && (i + k >= n || !alnum((unsigned char)line[i + k]));
}

static int def_find(Compiler *c, const char *name) {
    return ht_find_grid(c->ht_def, HT_DEF_N, name, c->def_name[0], NAME_MAX);
}

static void add_def(Compiler *c, const char *name, const char *body) {
    int di;
    if (!name || c->ndef >= DEF_MAX) {
        return;
    }
    di = def_find(c, name);
    if (di < 0) {
        di = c->ndef++;
        text(c->def_name[di], NAME_MAX, name);
        ht_ins_grid(c->ht_def, HT_DEF_N, name, di, c->def_name[0], NAME_MAX);
    }
    text(c->def_body[di], DEF_BODY, body ? body : "1");
    c->def_fn[di] = 0;
    c->def_nparam[di] = 0;
}

static void add_builtin_macros(Compiler *c) {
    add_def(c, "__CHRISC__", "1");
    add_def(c, "__GNUC__", "1");
    add_def(c, "NORMALUNIX", "1");
    add_def(c, "__BYTE_ORDER__", "1234");
    add_def(c, "__ORDER_LITTLE_ENDIAN__", "1234");
    add_def(c, "__ORDER_BIG_ENDIAN__", "4321");
}

static int sys_inc_map(const char *inc, char *out, int cap) {
    static const char *pairs[][2] = {
        {"stdio.h", "LIB/STDIO.H"},
        {"stdlib.h", "LIB/STDLIB.H"},
        {"string.h", "LIB/STRING.H"},
        {"strings.h", "LIB/STRINGS.H"},
        {"stddef.h", "LIB/STDDEF.H"},
        {"stdint.h", "LIB/STDINT.H"},
        {"stdarg.h", "LIB/STDARG.H"},
        {"ctype.h", "LIB/CTYPE.H"},
        {"math.h", "LIB/MATH.H"},
        {"setjmp.h", "LIB/SETJMP.H"},
        {"inttypes.h", "LIB/INTTYPES.H"},
        {"stdbool.h", "LIB/STDBOOL.H"},
        {"limits.h", "LIB/LIMITS.H"},
        {"assert.h", "LIB/ASSERT.H"},
        {"errno.h", "LIB/ERRNO.H"},
        {"unistd.h", "LIB/UNISTD.H"},
        {"fcntl.h", "LIB/FCNTL.H"},
        {"time.h", "LIB/TIME.H"},
        {"memory.h", "LIB/STRING.H"},
        {"sys/types.h", "LIB/SYS_TYPES.H"},
        {"sys/stat.h", "LIB/SYS_STAT.H"},
        {0, 0}
    };
    int i;
    if (!inc || !out || cap < 2) {
        return 0;
    }
    for (i = 0; pairs[i][0]; ++i) {
        if (same(inc, pairs[i][0])) {
            text(out, (size_t)cap, pairs[i][1]);
            return 1;
        }
    }
    return 0;
}

static int once_has(Compiler *c, const char *path) {
    int i;
    for (i = 0; i < c->pragma_once_n; ++i) {
        if (same(c->pragma_once[i], path)) {
            return 1;
        }
    }
    return 0;
}

static size_t compact_line_cont(char *s, size_t n) {
    size_t i = 0;
    size_t o = 0;
    if (!s) {
        return 0;
    }
    while (i < n) {
        if (s[i] == '\\' && i + 1 < n) {
            size_t j = i + 1;
            if (s[j] == '\r' && j + 1 < n && s[j + 1] == '\n') {
                s[o++] = ' ';
                i = j + 2;
                continue;
            }
            if (s[j] == '\n') {
                s[o++] = ' ';
                i = j + 1;
                continue;
            }
        }
        s[o++] = s[i++];
    }
    s[o] = 0;
    return o;
}

static int ident_at(const char *s, size_t n, size_t i, char *name, int cap) {
    size_t k = 0;
    if (i >= n || !alpha((unsigned char)s[i])) {
        return 0;
    }
    while (i < n && alnum((unsigned char)s[i]) && k + 1 < (size_t)cap) {
        name[k++] = s[i++];
    }
    name[k] = 0;
    return (int)k;
}

static int slen_local(const char *s);
static int pp_eval_expr(Compiler *c, const char *s, size_t n, size_t *i);

static int pp_skip_ws(const char *s, size_t n, size_t *i) {
    while (*i < n && (s[*i] == ' ' || s[*i] == '\t')) {
        *i += 1;
    }
    return *i < n;
}

static int pp_eval_prim(Compiler *c, const char *s, size_t n, size_t *i) {
    char name[NAME_MAX];
    int k;
    int d;
    int v = 0;
    int neg = 0;
    if (!pp_skip_ws(s, n, i)) {
        return 0;
    }
    if (s[*i] == '!') {
        *i += 1;
        return !pp_eval_prim(c, s, n, i);
    }
    if (s[*i] == '~') {
        *i += 1;
        return ~pp_eval_prim(c, s, n, i);
    }
    if (s[*i] == '-') {
        *i += 1;
        neg = 1;
    }
    if (*i < n && s[*i] == '(') {
        *i += 1;
        v = pp_eval_expr(c, s, n, i);
        pp_skip_ws(s, n, i);
        if (*i < n && s[*i] == ')') {
            *i += 1;
        }
        return neg ? -v : v;
    }
    k = ident_at(s, n, *i, name, NAME_MAX);
    if (k) {
        *i += (size_t)k;
        if (same(name, "defined")) {
            int paren = 0;
            pp_skip_ws(s, n, i);
            if (*i < n && s[*i] == '(') {
                paren = 1;
                *i += 1;
                pp_skip_ws(s, n, i);
            }
            k = ident_at(s, n, *i, name, NAME_MAX);
            *i += (size_t)k;
            pp_skip_ws(s, n, i);
            if (paren && *i < n && s[*i] == ')') {
                *i += 1;
            }
            return def_find(c, name) >= 0;
        }
        d = def_find(c, name);
        if (d >= 0) {
            size_t j = 0;
            return pp_eval_expr(c, c->def_body[d],
                                (size_t)slen_local(c->def_body[d]), &j);
        }
        return 0;
    }
    while (*i < n && digit((unsigned char)s[*i])) {
        v = v * 10 + (s[*i] - '0');
        *i += 1;
    }
    return neg ? -v : v;
}

static int slen_local(const char *s) {
    int n = 0;
    while (s[n]) {
        ++n;
    }
    return n;
}

static int pp_eval_bin(Compiler *c, const char *s, size_t n, size_t *i, int lhs, int minp);

static int prec_op(const char *s, size_t n, size_t i, int *oplen) {
    if (i + 1 < n && s[i] == '&' && s[i + 1] == '&') { *oplen = 2; return 2; }
    if (i + 1 < n && s[i] == '|' && s[i + 1] == '|') { *oplen = 2; return 1; }
    if (i + 1 < n && s[i] == '=' && s[i + 1] == '=') { *oplen = 2; return 4; }
    if (i + 1 < n && s[i] == '!' && s[i + 1] == '=') { *oplen = 2; return 4; }
    if (i + 1 < n && s[i] == '<' && s[i + 1] == '=') { *oplen = 2; return 5; }
    if (i + 1 < n && s[i] == '>' && s[i + 1] == '=') { *oplen = 2; return 5; }
    if (i + 1 < n && s[i] == '<' && s[i + 1] == '<') { *oplen = 2; return 6; }
    if (i + 1 < n && s[i] == '>' && s[i + 1] == '>') { *oplen = 2; return 6; }
    if (s[i] == '<') { *oplen = 1; return 5; }
    if (s[i] == '>') { *oplen = 1; return 5; }
    if (s[i] == '+') { *oplen = 1; return 7; }
    if (s[i] == '-') { *oplen = 1; return 7; }
    if (s[i] == '*') { *oplen = 1; return 8; }
    if (s[i] == '/') { *oplen = 1; return 8; }
    if (s[i] == '%') { *oplen = 1; return 8; }
    if (s[i] == '&') { *oplen = 1; return 3; }
    if (s[i] == '|') { *oplen = 1; return 3; }
    if (s[i] == '^') { *oplen = 1; return 3; }
    *oplen = 0;
    return 0;
}

static int apply_op(int a, int b, const char *s, size_t i, int oplen) {
    if (oplen == 2 && s[i] == '&' && s[i + 1] == '&') return a && b;
    if (oplen == 2 && s[i] == '|' && s[i + 1] == '|') return a || b;
    if (oplen == 2 && s[i] == '=' && s[i + 1] == '=') return a == b;
    if (oplen == 2 && s[i] == '!' && s[i + 1] == '=') return a != b;
    if (oplen == 2 && s[i] == '<' && s[i + 1] == '=') return a <= b;
    if (oplen == 2 && s[i] == '>' && s[i + 1] == '=') return a >= b;
    if (oplen == 2 && s[i] == '<' && s[i + 1] == '<') return a << b;
    if (oplen == 2 && s[i] == '>' && s[i + 1] == '>') return a >> b;
    if (s[i] == '<') return a < b;
    if (s[i] == '>') return a > b;
    if (s[i] == '+') return a + b;
    if (s[i] == '-') return a - b;
    if (s[i] == '*') return a * b;
    if (s[i] == '/' ) return b ? a / b : 0;
    if (s[i] == '%') return b ? a % b : 0;
    if (s[i] == '&') return a & b;
    if (s[i] == '|') return a | b;
    if (s[i] == '^') return a ^ b;
    return 0;
}

static int pp_eval_expr(Compiler *c, const char *s, size_t n, size_t *i) {
    int lhs = pp_eval_prim(c, s, n, i);
    return pp_eval_bin(c, s, n, i, lhs, 1);
}

static int pp_eval_bin(Compiler *c, const char *s, size_t n, size_t *i, int lhs, int minp) {
    for (;;) {
        int p;
        int oplen = 0;
        size_t opi;
        int rhs;
        int p2;
        int oplen2 = 0;
        pp_skip_ws(s, n, i);
        if (*i >= n) {
            return lhs;
        }
        p = prec_op(s, n, *i, &oplen);
        if (p < minp || oplen == 0) {
            return lhs;
        }
        opi = *i;
        *i += (size_t)oplen;
        rhs = pp_eval_prim(c, s, n, i);
        for (;;) {
            pp_skip_ws(s, n, i);
            p2 = prec_op(s, n, *i, &oplen2);
            if (p2 <= p || oplen2 == 0) {
                break;
            }
            rhs = pp_eval_bin(c, s, n, i, rhs, p + 1);
        }
        lhs = apply_op(lhs, rhs, s, opi, oplen);
    }
}

static int expand_macros_line(Compiler *c, const char *in, size_t n, char *out, int cap) {
    size_t i = 0;
    int o = 0;
    static int edepth;
    while (i < n) {
        char name[NAME_MAX];
        int k;
        int d;
        if (in[i] == '"' || in[i] == '\'') {
            char q = in[i];
            if (o + 1 < cap) {
                out[o++] = in[i];
            }
            ++i;
            while (i < n && in[i] != q) {
                if (in[i] == '\\' && i + 1 < n) {
                    if (o + 1 < cap) {
                        out[o++] = in[i];
                    }
                    ++i;
                }
                if (o + 1 < cap) {
                    out[o++] = in[i];
                }
                ++i;
            }
            if (i < n) {
                if (o + 1 < cap) {
                    out[o++] = in[i];
                }
                ++i;
            }
            continue;
        }
        if (alpha((unsigned char)in[i])) {
            k = ident_at(in, n, i, name, NAME_MAX);
            i += (size_t)k;
            if (same(name, "__LINE__")) {
                int v = c->pp_line > 0 ? c->pp_line : 1;
                char buf[16];
                int bi = 0;
                int p;
                if (v == 0) {
                    buf[bi++] = '0';
                } else {
                    char rev[16];
                    int ri = 0;
                    while (v > 0 && ri < 15) {
                        rev[ri++] = (char)('0' + (v % 10));
                        v /= 10;
                    }
                    while (ri) {
                        buf[bi++] = rev[--ri];
                    }
                }
                for (p = 0; p < bi && o + 1 < cap; p++) {
                    out[o++] = buf[p];
                }
                continue;
            }
            if (same(name, "__FILE__")) {
                const char *fp = "\"\"";
                int p;
                if (c->pp_file >= 0 && c->pp_file < c->nfiles) {
                    fp = c->files[c->pp_file];
                }
                if (o + 1 < cap) {
                    out[o++] = '"';
                }
                for (p = 0; fp[p] && o + 1 < cap; p++) {
                    out[o++] = fp[p];
                }
                if (o + 1 < cap) {
                    out[o++] = '"';
                }
                continue;
            }
            d = def_find(c, name);
            if (d >= 0 && !c->def_fn[d]) {
                if (edepth < 8) {
                    char inner[DEF_BODY];
                    size_t bn = 0;
                    int ilen;
                    int b = 0;
                    while (c->def_body[d][bn]) {
                        bn++;
                    }
                    edepth++;
                    ilen = expand_macros_line(c, c->def_body[d], bn, inner,
                                             DEF_BODY);
                    edepth--;
                    while (b < ilen && o + 1 < cap) {
                        out[o++] = inner[b++];
                    }
                } else {
                    int b = 0;
                    while (c->def_body[d][b] && o + 1 < cap) {
                        out[o++] = c->def_body[d][b++];
                    }
                }
            } else if (d >= 0 && c->def_fn[d]) {
                char args[DEF_PARAM_MAX][256];
                int nargs = 0;
                int p;
                while (i < n && (in[i] == ' ' || in[i] == '\t')) {
                    ++i;
                }
                for (p = 0; p < DEF_PARAM_MAX; ++p) {
                    args[p][0] = 0;
                }
                if (i < n && in[i] == '(') {
                    int depth = 0;
                    int ai = 0;
                    int inq = 0;
                    ++i;
                    nargs = 1;
                    while (i < n && !(depth == 0 && !inq && in[i] == ')')) {
                        if (!inq && in[i] == '(') {
                            ++depth;
                        } else if (!inq && in[i] == ')') {
                            --depth;
                        } else if (in[i] == '"' || in[i] == '\'') {
                            inq = inq ? 0 : 1;
                        } else if (inq && in[i] == '\\' && i + 1 < n) {
                            if (nargs > 0 && nargs <= DEF_PARAM_MAX && ai < 255) {
                                args[nargs - 1][ai++] = in[i];
                                args[nargs - 1][ai] = 0;
                            }
                            ++i;
                        }
                        if (!inq && depth == 0 && in[i] == ',') {
                            if (ai < 255)
                                args[nargs - 1][ai] = 0;
                            if (nargs < DEF_PARAM_MAX) {
                                nargs++;
                                ai = 0;
                            }
                            ++i;
                            continue;
                        }
                        if (nargs > 0 && nargs <= DEF_PARAM_MAX && ai < 255) {
                            args[nargs - 1][ai++] = in[i];
                            args[nargs - 1][ai] = 0;
                        }
                        ++i;
                    }
                    if (i < n && in[i] == ')') {
                        ++i;
                    }
                    if (nargs == 1 && args[0][0] == 0) {
                        nargs = 0;
                    }
                } else {
                    int t = 0;
                    while (name[t] && o + 1 < cap) {
                        out[o++] = name[t++];
                    }
                    continue;
                }
                {
                    char produced[DEF_BODY];
                    int po = 0;
                    int b = 0;
                    const char *body = c->def_body[d];
                    while (body[b] && po + 1 < DEF_BODY) {
                        if (body[b] == '#' && body[b + 1] == '#') {
                            b += 2;
                            continue;
                        }
                        if (body[b] == '#' && alpha((unsigned char)body[b + 1])) {
                            char pn[NAME_MAX];
                            int pk = ident_at(body, 512, (size_t)(b + 1), pn, NAME_MAX);
                            int pi;
                            produced[po++] = '"';
                            for (pi = 0; pi < c->def_nparam[d]; ++pi) {
                                if (same(c->def_param[d][pi], pn)) {
                                    int a = 0;
                                    while (args[pi][a] && po + 1 < DEF_BODY) {
                                        produced[po++] = args[pi][a++];
                                    }
                                    break;
                                }
                            }
                            if (po + 1 < DEF_BODY)
                                produced[po++] = '"';
                            b += 1 + pk;
                            continue;
                        }
                        if (alpha((unsigned char)body[b])) {
                            char pn[NAME_MAX];
                            int pk = ident_at(body, 512, (size_t)b, pn, NAME_MAX);
                            int pi;
                            int sub = 0;
                            for (pi = 0; pi < c->def_nparam[d]; ++pi) {
                                if (same(c->def_param[d][pi], pn)) {
                                    if (edepth < 8) {
                                        char inner[256];
                                        size_t alen = 0;
                                        int ilen;
                                        int a = 0;
                                        while (args[pi][alen]) {
                                            alen++;
                                        }
                                        edepth++;
                                        ilen = expand_macros_line(c, args[pi], alen,
                                                                 inner, 256);
                                        edepth--;
                                        while (a < ilen && po + 1 < DEF_BODY) {
                                            produced[po++] = inner[a++];
                                        }
                                    } else {
                                        int a = 0;
                                        while (args[pi][a] && po + 1 < DEF_BODY) {
                                            produced[po++] = args[pi][a++];
                                        }
                                    }
                                    sub = 1;
                                    break;
                                }
                            }
                            if (!sub) {
                                int t = 0;
                                while (pn[t] && po + 1 < DEF_BODY) {
                                    produced[po++] = pn[t++];
                                }
                            }
                            b += pk;
                            continue;
                        }
                        produced[po++] = body[b++];
                    }
                    produced[po] = 0;
                    if (edepth < 8) {
                        char inner[DEF_BODY];
                        int ilen;
                        int a = 0;
                        edepth++;
                        ilen = expand_macros_line(c, produced, (size_t)po, inner,
                                                 DEF_BODY);
                        edepth--;
                        while (a < ilen && o + 1 < cap) {
                            out[o++] = inner[a++];
                        }
                    } else {
                        int a = 0;
                        while (a < po && o + 1 < cap) {
                            out[o++] = produced[a++];
                        }
                    }
                }
            } else {
                int b = 0;
                while (name[b] && o + 1 < cap) {
                    out[o++] = name[b++];
                }
            }
        } else {
            if (o + 1 < cap) {
                out[o++] = in[i];
            }
            ++i;
        }
    }
    if (o < cap) {
        out[o] = 0;
    }
    return o;
}

static int pp_unbalanced(const char *s, size_t n) {
    size_t i = 0;
    int depth = 0;
    int inq = 0;
    int in_char = 0;
    while (i < n) {
        if (!inq && !in_char && s[i] == '/' && i + 1 < n && s[i + 1] == '/') {
            break;
        }
        if (!inq && !in_char && s[i] == '/' && i + 1 < n && s[i + 1] == '*') {
            i += 2;
            while (i + 1 < n && !(s[i] == '*' && s[i + 1] == '/')) {
                ++i;
            }
            if (i + 1 < n) {
                i += 2;
            }
            continue;
        }
        if (!in_char && s[i] == '"') {
            inq = !inq;
        } else if (!inq && s[i] == '\'') {
            in_char = !in_char;
        } else if ((inq || in_char) && s[i] == '\\' && i + 1 < n) {
            ++i;
        } else if (!inq && !in_char && s[i] == '(') {
            depth++;
        } else if (!inq && !in_char && s[i] == ')') {
            depth--;
        }
        ++i;
    }
    return inq || in_char || depth > 0;
}

static int expand_file(Compiler *c, const char *path, const char *src, size_t n,
                       size_t *ulen, int *unit_line, int depth) {
    size_t i = 0;
    int orig_line = 1;
    int fid;

    if (once_has(c, path ? path : "")) {
        return 1;
    }
    if (depth > INCLUDE_DEPTH) {
        return fail(c, 1, 1, "include nested too deep");
    }
    fid = file_id_add(c, path ? path : "");
    if (fid < 0) {
        return fail(c, 1, 1, "too many include files");
    }
    {
        static int open_ids[INCLUDE_DEPTH + 1];
        int d;
        if (depth == 0) {
            open_ids[0] = fid;
        } else {
            for (d = 0; d < depth; ++d) {
                if (open_ids[d] == fid) {
                    return 1;
                }
            }
            if (depth <= INCLUDE_DEPTH) {
                open_ids[depth] = fid;
            }
        }
    }
    if (!    map_push(c, *unit_line, fid, orig_line)) {
        return fail(c, 1, 1, "include map full");
    }
    while (i < n) {
        size_t line_beg = i;
        size_t line_n;
        char inc[FILE_NAME_MAX];
        maybe_yield();
        while (i < n && src[i] != '\n') {
            ++i;
        }
        line_n = i - line_beg;
        {
            size_t kw = 0;
            int is_hash = line_starts_hash(src + line_beg, line_n, &kw);
            int skip = 0;
            int d;
            for (d = 1; d <= c->pp_depth; ++d) {
                if (!c->pp_true[d]) {
                    skip = 1;
                }
            }
            if (is_hash) {
                const char *ln = src + line_beg;
                if (kw_is(ln, line_n, kw, "ifdef") ||
                    kw_is(ln, line_n, kw, "ifndef") ||
                    kw_is(ln, line_n, kw, "if")) {
                    char name[NAME_MAX];
                    int take = 0;
                    size_t p = kw;
                    while (p < line_n && alnum((unsigned char)ln[p])) {
                        ++p;
                    }
                    while (p < line_n && (ln[p] == ' ' || ln[p] == '\t')) {
                        ++p;
                    }
                    if (c->pp_depth + 1 >= 16) {
                        return fail(c, orig_line, 1, "#if nested too deep");
                    }
                    c->pp_depth++;
                    if (skip) {
                        c->pp_true[c->pp_depth] = 0;
                        c->pp_taken[c->pp_depth] = 0;
                    } else if (kw_is(ln, line_n, kw, "ifdef") ||
                               kw_is(ln, line_n, kw, "ifndef")) {
                        ident_at(ln, line_n, p, name, NAME_MAX);
                        take = def_find(c, name) >= 0;
                        if (kw_is(ln, line_n, kw, "ifndef")) {
                            take = !take;
                        }
                        c->pp_true[c->pp_depth] = take;
                        c->pp_taken[c->pp_depth] = take;
                    } else {
                        size_t ei = p;
                        take = pp_eval_expr(c, ln, line_n, &ei) != 0;
                        c->pp_true[c->pp_depth] = take;
                        c->pp_taken[c->pp_depth] = take;
                    }
                } else if (kw_is(ln, line_n, kw, "else") ||
                           kw_is(ln, line_n, kw, "elif")) {
                    int parent = 1;
                    for (d = 1; d < c->pp_depth; ++d) {
                        if (!c->pp_true[d]) {
                            parent = 0;
                        }
                    }
                    if (c->pp_depth <= 0) {
                        return fail(c, orig_line, 1, "stray #else");
                    }
                    if (kw_is(ln, line_n, kw, "else")) {
                        c->pp_true[c->pp_depth] =
                            parent && !c->pp_taken[c->pp_depth];
                        c->pp_taken[c->pp_depth] = 1;
                    } else {
                        size_t p = kw;
                        size_t ei;
                        while (p < line_n && alnum((unsigned char)ln[p])) {
                            ++p;
                        }
                        while (p < line_n && (ln[p] == ' ' || ln[p] == '\t')) {
                            ++p;
                        }
                        ei = p;
                        if (c->pp_taken[c->pp_depth] || !parent) {
                            c->pp_true[c->pp_depth] = 0;
                        } else {
                            c->pp_true[c->pp_depth] =
                                pp_eval_expr(c, ln, line_n, &ei) != 0;
                            c->pp_taken[c->pp_depth] = c->pp_true[c->pp_depth];
                        }
                    }
                } else if (kw_is(ln, line_n, kw, "endif")) {
                    if (c->pp_depth <= 0) {
                        return fail(c, orig_line, 1, "stray #endif");
                    }
                    c->pp_depth--;
                } else if (!skip && kw_is(ln, line_n, kw, "define")) {
                    size_t p = kw;
                    char name[NAME_MAX];
                    int k;
                    int di;
                    while (p < line_n && alnum((unsigned char)ln[p])) {
                        ++p;
                    }
                    while (p < line_n && (ln[p] == ' ' || ln[p] == '\t')) {
                        ++p;
                    }
                    k = ident_at(ln, line_n, p, name, NAME_MAX);
                    p += (size_t)k;
                    di = def_find(c, name);
                    if (di < 0) {
                        if (c->ndef >= DEF_MAX) {
                            return fail(c, orig_line, 1, "too many #define");
                        }
                        di = c->ndef++;
                        text(c->def_name[di], NAME_MAX, name);
                        ht_ins_grid(c->ht_def, HT_DEF_N, name, di, c->def_name[0],
                                    NAME_MAX);
                    }
                    c->def_fn[di] = 0;
                    c->def_nparam[di] = 0;
                    if (p < line_n && ln[p] == '(') {
                        c->def_fn[di] = 1;
                        ++p;
                        while (p < line_n && ln[p] != ')') {
                            char pn[NAME_MAX];
                            int pk;
                            while (p < line_n && (ln[p] == ' ' || ln[p] == '\t' ||
                                                  ln[p] == ',')) {
                                ++p;
                            }
                            if (p >= line_n || ln[p] == ')') {
                                break;
                            }
                            pk = ident_at(ln, line_n, p, pn, NAME_MAX);
                            if (pk <= 0) {
                                break;
                            }
                            p += (size_t)pk;
                            if (c->def_nparam[di] < DEF_PARAM_MAX) {
                                text(c->def_param[di][c->def_nparam[di]],
                                     DEF_PARAM_LEN, pn);
                                c->def_nparam[di]++;
                            }
                        }
                        if (p < line_n && ln[p] == ')') {
                            ++p;
                        }
                    }
                    while (p < line_n && (ln[p] == ' ' || ln[p] == '\t')) {
                        ++p;
                    }
                    {
                        int b = 0;
                        while (p < line_n && b + 1 < DEF_BODY) {
                            if (ln[p] == '/' && p + 1 < line_n &&
                                ln[p + 1] == '/') {
                                break;
                            }
                            if (ln[p] == '/' && p + 1 < line_n &&
                                ln[p + 1] == '*') {
                                p += 2;
                                while (p + 1 < line_n &&
                                       !(ln[p] == '*' && ln[p + 1] == '/')) {
                                    p++;
                                }
                                if (p + 1 < line_n) {
                                    p += 2;
                                }
                                continue;
                            }
                            c->def_body[di][b++] = ln[p++];
                        }
                        c->def_body[di][b] = 0;
                        if (p < line_n && !(ln[p] == '/' && p + 1 < line_n &&
                                            (ln[p + 1] == '/' || ln[p + 1] == '*'))) {
                            return fail(c, orig_line, 1, "macro body too long");
                        }
                    }
                } else if (!skip && kw_is(ln, line_n, kw, "undef")) {
                    size_t p = kw;
                    char name[NAME_MAX];
                    int di;
                    while (p < line_n && alnum((unsigned char)ln[p])) {
                        ++p;
                    }
                    while (p < line_n && (ln[p] == ' ' || ln[p] == '\t')) {
                        ++p;
                    }
                    ident_at(ln, line_n, p, name, NAME_MAX);
                    di = def_find(c, name);
                    if (di >= 0) {
                        c->def_name[di][0] = 0;
                    }
                } else if (!skip && kw_is(ln, line_n, kw, "error")) {
                    return fail(c, orig_line, 1, "#error");
                } else if (!skip && kw_is(ln, line_n, kw, "pragma")) {
                    size_t p = kw;
                    while (p < line_n && alnum((unsigned char)ln[p])) {
                        ++p;
                    }
                    while (p < line_n && (ln[p] == ' ' || ln[p] == '\t')) {
                        ++p;
                    }
                    if (p + 4 <= line_n && ln[p] == 'o' && ln[p + 1] == 'n' &&
                        ln[p + 2] == 'c' && ln[p + 3] == 'e') {
                        if (c->pragma_once_n < PRAGMA_ONCE_MAX) {
                            text(c->pragma_once[c->pragma_once_n], FILE_NAME_MAX,
                                 path ? path : "");
                            c->pragma_once_n++;
                        }
                    } else if (p + 3 <= line_n && ln[p] == 'a' && ln[p + 1] == 'b' &&
                               ln[p + 2] == 'i') {
                        size_t q = p + 3;
                        unsigned maj = 0;
                        unsigned min = 0;
                        while (q < line_n && (ln[q] == ' ' || ln[q] == '\t' ||
                                              ln[q] == '(')) {
                            ++q;
                        }
                        while (q < line_n && ln[q] >= '0' && ln[q] <= '9') {
                            maj = maj * 10u + (unsigned)(ln[q] - '0');
                            ++q;
                        }
                        while (q < line_n && (ln[q] == ' ' || ln[q] == '\t' ||
                                              ln[q] == ',')) {
                            ++q;
                        }
                        while (q < line_n && ln[q] >= '0' && ln[q] <= '9') {
                            min = min * 10u + (unsigned)(ln[q] - '0');
                            ++q;
                        }
                        c->abi_major = (uint16_t)maj;
                        c->abi_minor = (uint16_t)min;
                    } else if (p + 4 <= line_n && ln[p] == 'p' && ln[p + 1] == 'a' &&
                               ln[p + 2] == 'c' && ln[p + 3] == 'k') {
                        size_t q = p + 4;
                        while (q < line_n && (ln[q] == ' ' || ln[q] == '\t' ||
                                              ln[q] == '(')) {
                            ++q;
                        }
                        if (q < line_n && ln[q] == '1') {
                            c->pack_pragma = 1;
                        } else if (q < line_n && ln[q] == '0') {
                            c->pack_pragma = 0;
                        } else if (q < line_n && ln[q] == ')') {
                            c->pack_pragma = 0;
                        }
                    }
                } else if (!skip && line_is_include(ln, line_n, inc,
                                                   FILE_NAME_MAX)) {
                    if (!expand_include(c, path, inc, ulen, unit_line, depth)) {
                        return 0;
                    }
                    ++orig_line;
                    if (!map_push(c, *unit_line, fid, orig_line)) {
                        return fail(c, orig_line, 1, "include map full");
                    }
                    if (i < n && src[i] == '\n') {
                        ++i;
                    }
                    continue;
                } else if (!skip && kw_is(ln, line_n, kw, "line")) {
                    /* ignore */
                }
            } else if (!skip) {
                int elen;
                int k;
                int nphys = 1;
                size_t o = 0;
                size_t cap = sizeof(g_logical);
                size_t cpy = line_n;
                if (cpy >= cap) {
                    cpy = cap - 1;
                }
                while (o < cpy) {
                    g_logical[o] = src[line_beg + o];
                    o++;
                }
                while (pp_unbalanced(g_logical, o) && i < n && src[i] == '\n') {
                    size_t nbeg = i + 1;
                    size_t nxt = nbeg;
                    size_t nn;
                    size_t kw = 0;
                    while (nxt < n && src[nxt] != '\n') {
                        ++nxt;
                    }
                    nn = nxt - nbeg;
                    if (nbeg < n && line_starts_hash(src + nbeg, nn, &kw)) {
                        break;
                    }
                    if (o + 1 < cap) {
                        g_logical[o++] = '\n';
                    }
                    {
                        size_t p;
                        for (p = 0; p < nn && o + 1 < cap; p++) {
                            g_logical[o++] = src[nbeg + p];
                        }
                    }
                    i = nxt;
                    nphys++;
                }
                g_logical[o] = 0;
                c->pp_line = orig_line;
                c->pp_file = fid;
                elen = expand_macros_line(c, g_logical, o, g_line_exp,
                                          (int)sizeof(g_line_exp));
                for (k = 0; k < elen; ++k) {
                    if (!unit_put(ulen, (unsigned char)g_line_exp[k])) {
                        return fail(c, orig_line, 1, "source too large");
                    }
                    if (g_line_exp[k] == '\n') {
                        *unit_line += 1;
                    }
                }
                if (i < n && src[i] == '\n') {
                    if (!unit_put(ulen, '\n')) {
                        return fail(c, orig_line, 1, "source too large");
                    }
                    *unit_line += 1;
                    orig_line += nphys;
                } else {
                    orig_line += nphys - 1;
                }
                if (i < n && src[i] == '\n') {
                    ++i;
                }
                continue;
            }
            if (i < n && src[i] == '\n') {
                if (!unit_put(ulen, '\n')) {
                    return fail(c, orig_line, 1, "source too large");
                }
                *unit_line += 1;
                ++orig_line;
                ++i;
            }
            continue;
        }
    }
    return 1;
}

static int lex(Compiler *c, const char *s, size_t n) {
    size_t i = 0;
    int line = 1;
    int col = 1;

    if (n == 0 || n > CHRIS_SOURCE_MAX) {
        return fail(c, 1, 1, "source size outside limit");
    }
    while (i < n) {
        int ch = (unsigned char)s[i];
        int start = col;
        maybe_yield();

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
        if (ch == '/' && i + 1 < n && s[i + 1] == '*') {
            i += 2;
            col += 2;
            while (i + 1 < n && !(s[i] == '*' && s[i + 1] == '/')) {
                if (s[i] == '\n') {
                    ++line;
                    col = 1;
                } else {
                    ++col;
                }
                ++i;
            }
            if (i + 1 >= n) {
                return fail(c, line, start, "unterminated comment");
            }
            i += 2;
            col += 2;
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
        if (ch == '"') {
            int start_off;
            ++i;
            ++col;
            start_off = c->str_len;
            while (i < n && s[i] != '"' && s[i] != '\n') {
                int b = (unsigned char)s[i];
                if (s[i] == '\\' && i + 1 < n) {
                    char e = s[i + 1];
                    if (e == 'n') {
                        b = '\n';
                    } else if (e == 't') {
                        b = '\t';
                    } else if (e == '0') {
                        b = 0;
                    } else {
                        b = (unsigned char)e;
                    }
                    i += 2;
                    col += 2;
                } else {
                    ++i;
                    ++col;
                }
                if (c->str_len + 1 >= STR_POOL_MAX) {
                    return fail(c, line, start, "string pool full");
                }
                c->str_pool[c->str_len++] = (char)b;
            }
            if (i >= n || s[i] != '"') {
                return fail(c, line, start, "unterminated string");
            }
            ++i;
            ++col;
            for (;;) {
                while (i < n && (s[i] == ' ' || s[i] == '\t' || s[i] == '\r')) {
                    ++i;
                    ++col;
                }
                if (i < n && s[i] == '\n') {
                    ++i;
                    ++line;
                    col = 1;
                    continue;
                }
                if (i >= n || s[i] != '"') {
                    break;
                }
                ++i;
                ++col;
                while (i < n && s[i] != '"' && s[i] != '\n') {
                    int b2 = (unsigned char)s[i];
                    if (s[i] == '\\' && i + 1 < n) {
                        char e = s[i + 1];
                        if (e == 'n') {
                            b2 = '\n';
                        } else if (e == 't') {
                            b2 = '\t';
                        } else if (e == '0') {
                            b2 = 0;
                        } else {
                            b2 = (unsigned char)e;
                        }
                        i += 2;
                        col += 2;
                    } else {
                        ++i;
                        ++col;
                    }
                    if (c->str_len + 1 >= STR_POOL_MAX) {
                        return fail(c, line, start, "string pool full");
                    }
                    c->str_pool[c->str_len++] = (char)b2;
                }
                if (i >= n || s[i] != '"') {
                    return fail(c, line, start, "unterminated string");
                }
                ++i;
                ++col;
            }
            if (c->str_len + 1 >= STR_POOL_MAX) {
                return fail(c, line, start, "string pool full");
            }
            c->str_pool[c->str_len++] = 0;
            if (!token(c, T_STR, line, start)) {
                return 0;
            }
            c->tokens[c->ntok - 1].value = start_off;
            continue;
        }
        if (ch == '\'') {
            int b;
            ++i;
            ++col;
            if (i >= n) {
                return fail(c, line, start, "unterminated char");
            }
            if (s[i] == '\\' && i + 1 < n) {
                char e = s[i + 1];
                if (e == 'n') {
                    b = '\n';
                } else if (e == 't') {
                    b = '\t';
                } else if (e == '0') {
                    b = 0;
                } else {
                    b = (unsigned char)e;
                }
                i += 2;
                col += 2;
            } else {
                b = (unsigned char)s[i];
                ++i;
                ++col;
            }
            if (i >= n || s[i] != '\'') {
                return fail(c, line, start, "unterminated char");
            }
            ++i;
            ++col;
            if (!token(c, T_NUM, line, start)) {
                return 0;
            }
            c->tokens[c->ntok - 1].value = b;
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
            while (i < n && (s[i] == 'u' || s[i] == 'U' || s[i] == 'l' ||
                             s[i] == 'L')) {
                ++i;
                ++col;
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
        ONE('?', T_QUESTION);
        ONE(':', T_COLON);
        ONE('~', T_TILDE);
        case '+':
            if (i + 1 < n && s[i + 1] == '+') {
                if (!token(c, T_PLUSPLUS, line, start)) return 0;
                i += 2; col += 2;
            } else if (i + 1 < n && s[i + 1] == '=') {
                if (!token(c, T_PLUSEQ, line, start)) return 0;
                i += 2; col += 2;
            } else {
                if (!token(c, T_PLUS, line, start)) return 0;
                ++i; ++col;
            }
            break;
        case '-':
            if (i + 1 < n && s[i + 1] == '-') {
                if (!token(c, T_MINUSMINUS, line, start)) return 0;
                i += 2; col += 2;
            } else if (i + 1 < n && s[i + 1] == '=') {
                if (!token(c, T_MINUSEQ, line, start)) return 0;
                i += 2; col += 2;
            } else if (i + 1 < n && s[i + 1] == '>') {
                if (!token(c, T_ARROW, line, start)) return 0;
                i += 2; col += 2;
            } else {
                if (!token(c, T_MINUS, line, start)) return 0;
                ++i; ++col;
            }
            break;
        case '.':
            if (i + 2 < n && s[i + 1] == '.' && s[i + 2] == '.') {
                if (!token(c, T_ELLIPSIS, line, start)) return 0;
                i += 3; col += 3;
            } else {
                if (!token(c, T_DOT, line, start)) return 0;
                ++i; ++col;
            }
            break;
        case '*':
            if (i + 1 < n && s[i + 1] == '=') {
                if (!token(c, T_STAREQ, line, start)) return 0;
                i += 2; col += 2;
            } else {
                if (!token(c, T_STAR, line, start)) return 0;
                ++i; ++col;
            }
            break;
        case '/':
            if (i + 1 < n && s[i + 1] == '=') {
                if (!token(c, T_SLASHEQ, line, start)) return 0;
                i += 2; col += 2;
            } else {
                if (!token(c, T_SLASH, line, start)) return 0;
                ++i; ++col;
            }
            break;
        case '%':
            if (i + 1 < n && s[i + 1] == '=') {
                if (!token(c, T_PERCENTEQ, line, start)) return 0;
                i += 2; col += 2;
            } else {
                if (!token(c, T_PERCENT, line, start)) return 0;
                ++i; ++col;
            }
            break;
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
                if (!token(c, T_NOT, line, start)) {
                    return 0;
                }
                ++i;
                ++col;
            }
            break;
        case '&':
            if (i + 1 < n && s[i + 1] == '&') {
                if (!token(c, T_ANDAND, line, start)) {
                    return 0;
                }
                i += 2;
                col += 2;
            } else if (i + 1 < n && s[i + 1] == '=') {
                if (!token(c, T_AMPEQ, line, start)) {
                    return 0;
                }
                i += 2;
                col += 2;
            } else {
                if (!token(c, T_AMP, line, start)) {
                    return 0;
                }
                ++i;
                ++col;
            }
            break;
        case '|':
            if (i + 1 < n && s[i + 1] == '|') {
                if (!token(c, T_OROR, line, start)) {
                    return 0;
                }
                i += 2;
                col += 2;
            } else if (i + 1 < n && s[i + 1] == '=') {
                if (!token(c, T_PIPEEQ, line, start)) {
                    return 0;
                }
                i += 2;
                col += 2;
            } else {
                if (!token(c, T_PIPE, line, start)) {
                    return 0;
                }
                ++i;
                ++col;
            }
            break;
        case '^':
            if (i + 1 < n && s[i + 1] == '=') {
                if (!token(c, T_CARETEQ, line, start)) {
                    return 0;
                }
                i += 2;
                col += 2;
            } else {
                if (!token(c, T_CARET, line, start)) {
                    return 0;
                }
                ++i;
                ++col;
            }
            break;
        case '<':
            if (i + 1 < n && s[i + 1] == '<') {
                if (i + 2 < n && s[i + 2] == '=') {
                    if (!token(c, T_LSHEQ, line, start)) {
                        return 0;
                    }
                    i += 3;
                    col += 3;
                } else if (!token(c, T_LSH, line, start)) {
                    return 0;
                } else {
                    i += 2;
                    col += 2;
                }
            } else if (i + 1 < n && s[i + 1] == '=') {
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
            if (i + 1 < n && s[i + 1] == '>') {
                if (i + 2 < n && s[i + 2] == '=') {
                    if (!token(c, T_RSHEQ, line, start)) {
                        return 0;
                    }
                    i += 3;
                    col += 3;
                } else if (!token(c, T_RSH, line, start)) {
                    return 0;
                } else {
                    i += 2;
                    col += 2;
                }
            } else if (i + 1 < n && s[i + 1] == '=') {
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

static uint8_t take_qualifiers(Compiler *c) {
    uint8_t uns = 0;
    c->saw_static = 0;
    c->saw_const = 0;
    c->saw_tls = 0;
    for (;;) {
        if (take(c, T_UNSIGNED)) {
            uns = 1;
            continue;
        }
        if (take(c, T_SIGNED)) {
            uns = 0;
            continue;
        }
        if (take(c, T_STATIC)) {
            c->saw_static = 1;
            continue;
        }
        if (take(c, T_CONST)) {
            c->saw_const = 1;
            continue;
        }
        if (take(c, T_EXTERN) || take(c, T_VOLATILE) || take(c, T_INLINE) ||
            take(c, T_RESTRICT) || take(c, T_NORETURN)) {
            continue;
        }
        if (take(c, T_THREADLOCAL)) {
            c->saw_tls = 1;
            continue;
        }
        break;
    }
    return uns;
}

static void static_mangle(Compiler *c, const char *src, char *dst) {
    int n = 0;
    int tu = c->tu_id;
    int v;
    dst[0] = 0;
    if (!src || tu <= 0) {
        text(dst, NAME_MAX, src ? src : "");
        return;
    }
    dst[n++] = '_';
    dst[n++] = 's';
    v = tu;
    if (v >= 100 && n + 1 < NAME_MAX) {
        dst[n++] = (char)('0' + v / 100);
        v = v % 100;
    }
    if (c->tu_id >= 10 && n + 1 < NAME_MAX) {
        dst[n++] = (char)('0' + (c->tu_id / 10) % 10);
    }
    if (n + 1 < NAME_MAX) {
        dst[n++] = (char)('0' + c->tu_id % 10);
    }
    if (n + 1 < NAME_MAX) {
        dst[n++] = '_';
    }
    {
        int i = 0;
        while (src[i] && n + 1 < NAME_MAX) {
            dst[n++] = src[i++];
        }
    }
    dst[n] = 0;
}

static int skip_attr(Compiler *c) {
    int packed = 0;
    while (cur(c)->kind == T_ID &&
           (same(cur(c)->name, "__attribute__") ||
            same(cur(c)->name, "__attribute"))) {
        ++c->pos;
        if (take(c, T_LP)) {
            int depth = 1;
            while (depth && cur(c)->kind != T_EOF) {
                if (cur(c)->kind == T_ID && same(cur(c)->name, "packed")) {
                    packed = 1;
                }
                if (take(c, T_LP)) {
                    depth++;
                } else if (take(c, T_RP)) {
                    depth--;
                } else {
                    ++c->pos;
                }
            }
        }
    }
    return packed;
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
    n->sid = -1;
    n->name[0] = 0;
    return id;
}

static int scope_cur(const Compiler *c) {
    return c->scope_sp ? c->scope_stack[c->scope_sp - 1] : 0;
}

static int scope_enter(Compiler *c) {
    if (c->scope_sp == 32) {
        return 0;
    }
    c->scope_seq++;
    c->scope_stack[c->scope_sp++] = c->scope_seq;
    return 1;
}

static void scope_leave(Compiler *c) {
    if (c->scope_sp > 0) {
        c->scope_sp--;
    }
}

static int scope_visible(const Compiler *c, int sc) {
    int i;
    if (sc == 0) {
        return 1;
    }
    for (i = 0; i < c->scope_sp; ++i) {
        if (c->scope_stack[i] == (int)sc) {
            return 1;
        }
    }
    return 0;
}

static int sym_find_local(Compiler *c, const char *name) {
    int i;
    for (i = c->nsyms - 1; i >= 0; --i) {
        if (c->syms[i].is_global) {
            continue;
        }
        if (!scope_visible(c, (int)c->syms[i].scope)) {
            continue;
        }
        if (same(c->syms[i].name, name)) {
            return i;
        }
    }
    return -1;
}

static int sym_find(Compiler *c, const char *name) {
    int i = sym_find_local(c, name);
    if (i >= 0) {
        return i;
    }
    if (c->tu_id > 0) {
        char mang[NAME_MAX];
        static_mangle(c, name, mang);
        i = ht_find_grid(c->ht_sym, HT_SYM_N, mang, (const char *)c->syms,
                         (int)sizeof(Symbol));
        if (i >= 0 && c->syms[i].is_global) {
            return i;
        }
    }
    i = ht_find_grid(c->ht_sym, HT_SYM_N, name, (const char *)c->syms,
                     (int)sizeof(Symbol));
    if (i >= 0 && c->syms[i].is_global) {
        return i;
    }
    return -1;
}

static void decl_name(Compiler *c, Token *t, char *dst) {
    if (c->saw_static && scope_cur(c) == 0 && c->tu_id > 0) {
        static_mangle(c, t->name, dst);
    } else {
        text(dst, NAME_MAX, t->name);
    }
}

static int reuse_global_sym(Compiler *c, Token *t) {
    int i;
    char want[NAME_MAX];
    if (scope_cur(c) != 0) {
        return -1;
    }
    decl_name(c, t, want);
    i = ht_find_grid(c->ht_sym, HT_SYM_N, want, (const char *)c->syms,
                     (int)sizeof(Symbol));
    if (i >= 0 && c->syms[i].scope == 0) {
        return i;
    }
    return -1;
}

static int sym_add(Compiler *c, Token *t, uint8_t is_float, uint8_t width,
                   uint8_t is_ptr) {
    int i;
    i = reuse_global_sym(c, t);
    if (i >= 0) {
        return i;
    }
    for (i = c->nsyms - 1; i >= 0; --i) {
        if ((int)c->syms[i].scope != scope_cur(c)) {
            continue;
        }
        if (same(c->syms[i].name, t->name)) {
            fail(c, t->line, t->column, "duplicate variable");
            return -1;
        }
    }
    if (c->nsyms == SYM_MAX) {
        fail(c, t->line, t->column, "symbol table full");
        return -1;
    }
    if (width == 0)
        width = 4;
    {
        uint16_t pointee;
        uint32_t need;
        if (c->align_next > 1) {
            uint32_t a = (uint32_t)c->align_next;
            if ((a & (a - 1u)) == 0)
                c->mem_next = (c->mem_next + a - 1u) & ~(a - 1u);
            c->align_next = 0;
        }
        if (is_ptr) {
            /* `width` is always the pointee size (1=char*, 4=int*, 8=char**). */
            pointee = width ? width : 4u;
            width = 8;
        } else {
            pointee = 0;
        }
        need = is_ptr ? 8u : (width < 4 ? 4u : (uint32_t)width);
        i = c->nsyms++;
        decl_name(c, t, c->syms[i].name);
        c->syms[i].address = c->mem_next;
        c->syms[i].is_float = is_float;
        c->syms[i].packed = (uint8_t)(width == 1 && !is_ptr);
        c->syms[i].is_array = 0;
        c->syms[i].is_ptr = is_ptr;
        c->syms[i].is_unsigned = 0;
        c->syms[i].is_const = c->saw_const;
        c->syms[i].is_complex = 0;
        c->saw_const = 0;
        c->syms[i].is_global = 0;
        c->syms[i].scope = (uint16_t)scope_cur(c);
        c->syms[i].width = width;
        c->syms[i].pointee = pointee;
        c->syms[i].struct_id = -1;
        c->syms[i].stride = is_ptr
            ? (uint16_t)(pointee ? pointee : 4)
            : (uint16_t)need;
        c->syms[i].array_n = 0;
        c->syms[i].is_tls = 0;
        c->syms[i].tls_i = 0;
        if (c->saw_tls && scope_cur(c) == 0 && c->n_tls < 16) {
            c->syms[i].is_tls = 1;
            c->syms[i].tls_i = (uint16_t)c->n_tls++;
        }
        c->mem_next += need;
        if (c->syms[i].scope == 0) {
            ht_ins_grid(c->ht_sym, HT_SYM_N, c->syms[i].name, i,
                        (const char *)c->syms, (int)sizeof(Symbol));
        }
        return i;
    }
}

static uint8_t mem_ld(const Symbol *s) {
    if (s->is_float)
        return CL_OP_FLOAD;
    if (s->packed || s->width == 1)
        return CL_OP_LOADB;
    if (s->width >= 8 || s->is_ptr)
        return CL_OP_LOAD64;
    return CL_OP_LOAD;
}

static uint8_t mem_st(const Symbol *s) {
    if (s->is_float)
        return CL_OP_FSTORE;
    if (s->packed || s->width == 1)
        return CL_OP_STOREB;
    if (s->width >= 8 || s->is_ptr)
        return CL_OP_STORE64;
    return CL_OP_STORE;
}

static int sym_add_array(Compiler *c, Token *t, int32_t count, uint8_t is_float,
                         uint8_t packed, uint8_t elem_w) {
    int i;
    uint32_t need;
    uint16_t stride;

    if (count < 1 || count > 65536) {
        fail(c, t->line, t->column, "array size out of range");
        return -1;
    }
    i = reuse_global_sym(c, t);
    if (i >= 0) {
        return i;
    }
    i = sym_find_local(c, t->name);
    if (i >= 0) {
        fail(c, t->line, t->column, "duplicate variable");
        return -1;
    }
    if (c->nsyms == SYM_MAX) {
        fail(c, t->line, t->column, "symbol table full");
        return -1;
    }
    if (elem_w == 0)
        elem_w = packed ? 1 : 4;
    /* Do not cap stride at 8: packed WAD structs are 12–40 bytes, and
     * `filelump_t dir[n]` must step sizeof(filelump_t). */
    stride = packed ? 1u : (uint16_t)(elem_w < 4 ? 4 : elem_w);
    if (packed) {
        need = (uint32_t)((count + 3) & ~3);
    } else {
        need = (uint32_t)count * (uint32_t)stride;
    }
    if ((uint32_t)c->mem_next + need > CLVM_MEMORY_SIZE) {
        fail(c, t->line, t->column, "array exceeds CLVM memory");
        return -1;
    }
    i = c->nsyms++;
    decl_name(c, t, c->syms[i].name);
    c->syms[i].address = c->mem_next;
    c->syms[i].is_float = is_float;
    c->syms[i].packed = packed;
    c->syms[i].is_array = 1;
    c->syms[i].is_ptr = 0;
    c->syms[i].is_unsigned = 0;
    c->syms[i].is_const = 0;
    c->syms[i].is_complex = 0;
    c->syms[i].is_global = 0;
    c->syms[i].scope = (uint16_t)scope_cur(c);
    c->syms[i].width = packed ? 1 : elem_w;
    c->syms[i].pointee = 0;
    c->syms[i].struct_id = -1;
    c->syms[i].stride = stride;
    c->syms[i].array_n = (uint16_t)(count > 65535 ? 65535 : count);
    c->mem_next += need;
    if (c->syms[i].scope == 0) {
        ht_ins_grid(c->ht_sym, HT_SYM_N, c->syms[i].name, i,
                    (const char *)c->syms, (int)sizeof(Symbol));
    }
    return i;
}

static int sym_add_typedef_array(Compiler *c, Token *t, const DeclType *dt) {
    uint8_t ew = (uint8_t)(dt->elemw ? dt->elemw : 1u);
    uint8_t packed = (ew <= 1u) ? 1u : 0u;
    return sym_add_array(c, t, dt->arr_n > 0 ? dt->arr_n : 1, dt->isf, packed, ew);
}

static int typedef_find(Compiler *c, const char *name) {
    return ht_find_grid(c->ht_td, HT_TD_N, name, c->td_name[0], NAME_MAX);
}

static int struct_find(Compiler *c, const char *name) {
    return ht_find_grid(c->ht_st, HT_ST_N, name, (const char *)c->structs,
                        (int)sizeof(StructDef));
}

static int func_find(Compiler *c, const char *name) {
    int i;
    if (c->tu_id > 0) {
        char mang[NAME_MAX];
        static_mangle(c, name, mang);
        i = ht_find_grid(c->ht_func, HT_FUNC_N, mang, (const char *)c->funcs,
                         (int)sizeof(FuncDef));
        if (i >= 0) {
            return i;
        }
    }
    return ht_find_grid(c->ht_func, HT_FUNC_N, name, (const char *)c->funcs,
                        (int)sizeof(FuncDef));
}

static int const_find(Compiler *c, const char *name) {
    return ht_find_grid(c->ht_const, HT_CONST_N, name, c->const_name[0], NAME_MAX);
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

static int struct_add_field_ex(Compiler *c, StructDef *s, const char *name,
                               uint8_t isf, uint16_t w, uint16_t elemw,
                               int is_union, int line, int col, int16_t nested,
                               uint8_t is_ptr) {
    uint16_t need;
    if (s->nfields == FIELD_MAX) {
        return fail(c, line, col, "too many fields");
    }
    if (w == 0)
        w = 4;
    if (s->packed) {
        need = w;
    } else if (w <= 2) {
        need = 4;
    } else {
        need = w;
    }
    text(s->fname[s->nfields], NAME_MAX, name);
    s->is_float[s->nfields] = isf;
    s->fptr[s->nfields] = is_ptr;
    s->fwidth[s->nfields] = w;
    s->felemw[s->nfields] = elemw;
    s->foff[s->nfields] = is_union ? 0 : s->size;
    s->fstruct[s->nfields] = nested;
    s->fbits[s->nfields] = 0;
    s->fbitoff[s->nfields] = 0;
    if (c->field_bits > 0 && c->field_bits <= 32) {
        s->fbits[s->nfields] = (uint8_t)c->field_bits;
        s->fwidth[s->nfields] = 4;
        c->field_bits = 0;
    }
    s->nfields++;
    if (is_union) {
        if (need > s->size)
            s->size = need;
    } else {
        s->size += need;
    }
    return 1;
}

/* Trailing `} PACKEDATTR name;` is parsed after fields are laid out, so
 * recompute offsets from fwidth once packed/union is known. */
static void struct_recompute_layout(StructDef *s) {
    int i;
    uint16_t off = 0;
    uint16_t maxneed = 0;
    int bit_used = 0;
    uint16_t unit = 0;
    for (i = 0; i < s->nfields; ++i) {
        uint16_t w = s->fwidth[i] ? s->fwidth[i] : 4u;
        uint16_t need;
        if (s->fbits[i] > 0 && !s->is_union) {
            int bits = s->fbits[i];
            if (bit_used == 0 || bit_used + bits > 32) {
                if (off & 3u)
                    off = (uint16_t)((off + 3u) & ~3u);
                unit = off;
                bit_used = 0;
                off = (uint16_t)(off + 4u);
            }
            s->foff[i] = unit;
            s->fbitoff[i] = (uint8_t)bit_used;
            s->fwidth[i] = 4;
            bit_used += bits;
            continue;
        }
        bit_used = 0;
        if (s->packed) {
            need = w;
        } else if (w <= 2) {
            need = 4;
        } else {
            need = w;
        }
        if (s->is_union) {
            s->foff[i] = 0;
            if (need > maxneed) {
                maxneed = need;
            }
        } else {
            s->foff[i] = off;
            off = (uint16_t)(off + need);
        }
    }
    s->size = s->is_union ? maxneed : off;
}

/* `typedef struct Foo Foo;` is recorded before the body, so td_width is
 * still 4. Refresh when the struct is completed so `Foo *` strides sizeof(Foo). */
static void typedef_refresh_struct(Compiler *c, int sid) {
    int ti;
    uint16_t sz;
    if (sid < 0 || sid >= c->nstructs) {
        return;
    }
    sz = c->structs[sid].size;
    if (!sz) {
        return;
    }
    for (ti = 0; ti < c->ntypedef; ++ti) {
        if (c->td_struct[ti] == sid && !c->td_ptr[ti] && c->td_count[ti] <= 1) {
            c->td_width[ti] = (int)sz;
        }
    }
}

static uint16_t typedef_live_width(Compiler *c, int ti) {
    int sid;
    uint16_t w;
    if (ti < 0 || ti >= c->ntypedef) {
        return 4;
    }
    w = (uint16_t)(c->td_width[ti] ? c->td_width[ti] : 4);
    sid = c->td_struct[ti];
    if (!c->td_ptr[ti] && c->td_count[ti] <= 1 && sid >= 0 &&
        sid < c->nstructs && c->structs[sid].size) {
        w = c->structs[sid].size;
    }
    return w;
}

static int struct_add_field(Compiler *c, StructDef *s, const char *name,
                            uint8_t isf, uint16_t w, int is_union, int line,
                            int col, int16_t nested, uint8_t is_ptr) {
    return struct_add_field_ex(c, s, name, isf, w, 0, is_union, line, col,
                               nested, is_ptr);
}

static void struct_recompute_layout(StructDef *s);

static int ensure_complex(Compiler *c) {
    int sid = struct_find(c, "__Complex");
    StructDef *s;
    int i;
    if (sid >= 0)
        return sid;
    if (c->nstructs == STRUCT_MAX)
        return -1;
    s = &c->structs[c->nstructs];
    text(s->name, NAME_MAX, "__Complex");
    s->nfields = 0;
    s->size = 0;
    s->packed = 0;
    s->is_union = 0;
    for (i = 0; i < FIELD_MAX; ++i) {
        s->fstruct[i] = -1;
        s->fptr[i] = 0;
        s->fbits[i] = 0;
        s->fbitoff[i] = 0;
    }
    ht_ins_grid(c->ht_st, HT_ST_N, "__Complex", c->nstructs,
                (const char *)c->structs, (int)sizeof(StructDef));
    c->nstructs++;
    if (!struct_add_field(c, s, "re", 1, 4, 0, 1, 1, -1, 0) ||
        !struct_add_field(c, s, "im", 1, 4, 0, 1, 1, -1, 0))
        return -1;
    struct_recompute_layout(s);
    return c->nstructs - 1;
}

static int parse_type_n(Compiler *c, DeclType *d, char *name_out, int ncap);
static int skip_paren_depth(Compiler *c);

static int parse_const_int(Compiler *c, int *out);

static int parse_const_prim(Compiler *c, int *out) {
    int v;
    int neg = 0;
    int til = 0;
    while (take(c, T_PLUS)) {
    }
    if (take(c, T_MINUS)) {
        neg = 1;
    }
    if (take(c, T_TILDE)) {
        til = 1;
    }
    if (take(c, T_LP)) {
        if (!parse_const_int(c, &v) ||
            !expect(c, T_RP, "expected ) in constant")) {
            return 0;
        }
    } else if (cur(c)->kind == T_NUM) {
        v = cur(c)->value;
        ++c->pos;
    } else if (cur(c)->kind == T_ID) {
        int i;
        int found = 0;
        v = 0;
        i = const_find(c, cur(c)->name);
        if (i >= 0) {
            v = (int)c->const_val[i];
            found = 1;
        }
        if (!found) {
            return 0;
        }
        ++c->pos;
    } else {
        return 0;
    }
    if (til) {
        v = ~v;
    }
    if (neg) {
        v = -v;
    }
    *out = v;
    return 1;
}

static int parse_const_mul(Compiler *c, int *out) {
    int v;
    if (!parse_const_prim(c, &v)) {
        return 0;
    }
    for (;;) {
        int r;
        if (take(c, T_STAR)) {
            if (!parse_const_prim(c, &r)) {
                return 0;
            }
            v *= r;
        } else if (take(c, T_SLASH)) {
            if (!parse_const_prim(c, &r) || r == 0) {
                return 0;
            }
            v /= r;
        } else if (take(c, T_PERCENT)) {
            if (!parse_const_prim(c, &r) || r == 0) {
                return 0;
            }
            v %= r;
        } else {
            break;
        }
    }
    *out = v;
    return 1;
}

static int parse_const_add(Compiler *c, int *out) {
    int v;
    if (!parse_const_mul(c, &v)) {
        return 0;
    }
    for (;;) {
        int r;
        if (take(c, T_PLUS)) {
            if (!parse_const_mul(c, &r)) {
                return 0;
            }
            v += r;
        } else if (take(c, T_MINUS)) {
            if (!parse_const_mul(c, &r)) {
                return 0;
            }
            v -= r;
        } else {
            break;
        }
    }
    *out = v;
    return 1;
}

static int parse_const_shift(Compiler *c, int *out) {
    int v;
    if (!parse_const_add(c, &v)) {
        return 0;
    }
    for (;;) {
        int r;
        if (take(c, T_LSH)) {
            if (!parse_const_add(c, &r)) {
                return 0;
            }
            v <<= (r & 31);
        } else if (take(c, T_RSH)) {
            if (!parse_const_add(c, &r)) {
                return 0;
            }
            v >>= (r & 31);
        } else {
            break;
        }
    }
    *out = v;
    return 1;
}

static int parse_const_int(Compiler *c, int *out) {
    int v;
    if (!parse_const_shift(c, &v)) {
        return 0;
    }
    for (;;) {
        int r;
        if (take(c, T_AMP)) {
            if (!parse_const_shift(c, &r)) {
                return 0;
            }
            v &= r;
        } else if (take(c, T_CARET)) {
            if (!parse_const_shift(c, &r)) {
                return 0;
            }
            v ^= r;
        } else if (take(c, T_PIPE)) {
            if (!parse_const_shift(c, &r)) {
                return 0;
            }
            v |= r;
        } else {
            break;
        }
    }
    *out = v;
    return 1;
}

static int parse_enum_list(Compiler *c) {
    int ev = 0;
    while (!take(c, T_RB) && cur(c)->kind != T_EOF) {
        Token *en = cur(c);
        int val;
        if (!expect(c, T_ID, "expected enumerator")) {
            return 0;
        }
        val = ev;
        if (take(c, T_ASSIGN)) {
            if (!parse_const_int(c, &val)) {
                return fail(c, cur(c)->line, cur(c)->column, "expected enum value");
            }
        }
        if (c->nconst < ENUM_MAX) {
            text(c->const_name[c->nconst], NAME_MAX, en->name);
            c->const_val[c->nconst] = val;
            ht_ins_grid(c->ht_const, HT_CONST_N, en->name, c->nconst,
                        c->const_name[0], NAME_MAX);
            c->nconst++;
        }
        ev = val + 1;
        take(c, T_COMMA);
    }
    return 1;
}

static int parse_array_dim(Compiler *c, int *count) {
    int n = 256;
    if (take(c, T_RBRACK)) {
        *count = 256;
        return 1;
    }
    if (!parse_const_int(c, &n)) {
        return fail(c, cur(c)->line, cur(c)->column, "expected array size");
    }
    if (!expect(c, T_RBRACK, "expected ]")) {
        return 0;
    }
    while (take(c, T_LBRACK)) {
        int n2 = 1;
        if (!take(c, T_RBRACK)) {
            if (!parse_const_int(c, &n2) ||
                !expect(c, T_RBRACK, "expected ]")) {
                return 0;
            }
        }
        if (n2 < 1) {
            n2 = 1;
        }
        if (n > 1 && n2 > 2147483647 / n) {
            n = 2147483647;
        } else {
            n *= n2;
        }
    }
    if (n < 1) {
        n = 1;
    }
    *count = n;
    return 1;
}

static int parse_struct_def(Compiler *c, int is_union) {
    StructDef *s;
    char tag[NAME_MAX];
    int exist = -1;
    int i;
    if (cur(c)->kind == T_ID) {
        text(tag, NAME_MAX, cur(c)->name);
        ++c->pos;
        exist = struct_find(c, tag);
    } else {
        int n = c->nstructs;
        tag[0] = '_';
        tag[1] = 'a';
        tag[2] = 'n';
        tag[3] = 'o';
        tag[4] = 'n';
        tag[5] = (char)('0' + (n / 100) % 10);
        tag[6] = (char)('0' + (n / 10) % 10);
        tag[7] = (char)('0' + n % 10);
        tag[8] = 0;
    }
    if (exist >= 0) {
        s = &c->structs[exist];
        s->is_union = (uint8_t)is_union;
        if (skip_attr(c)) {
            s->packed = 1;
        }
        if (cur(c)->kind != T_LB) {
            return 1;
        }
        if (s->nfields > 0) {
            return fail(c, cur(c)->line, cur(c)->column, "duplicate struct");
        }
    } else {
        if (c->nstructs == STRUCT_MAX) {
            return fail(c, cur(c)->line, cur(c)->column, "too many structs");
        }
        s = &c->structs[c->nstructs++];
        text(s->name, NAME_MAX, tag);
        ht_ins_grid(c->ht_st, HT_ST_N, tag, c->nstructs - 1,
                    (const char *)c->structs, (int)sizeof(StructDef));
        s->nfields = 0;
        s->size = 0;
        s->packed = c->pack_pragma;
        s->is_union = (uint8_t)is_union;
        for (i = 0; i < FIELD_MAX; ++i) {
            s->fstruct[i] = -1;
            s->fptr[i] = 0;
            s->fbits[i] = 0;
            s->fbitoff[i] = 0;
        }
        if (skip_attr(c)) {
            s->packed = 1;
        }
        if (cur(c)->kind != T_LB) {
            return 1;
        }
    }
    if (!expect(c, T_LB, "expected {")) {
        return 0;
    }
    while (!take(c, T_RB) && cur(c)->kind != T_EOF) {
        DeclType dt;
        Token *fn;
        Token fn_store;
        char fnbuf[NAME_MAX];
        uint16_t fw;
        int16_t nested;
        int count = 1;
        take(c, T_CONST);
        take(c, T_VOLATILE);
        if (cur(c)->kind == T_UNION || cur(c)->kind == T_STRUCT) {
            int inner;
            int nested_union = take(c, T_UNION);
            int is_ptr = 0;
            char tag[NAME_MAX];
            if (!nested_union) {
                take(c, T_STRUCT);
            }
            tag[0] = 0;
            if (cur(c)->kind == T_ID) {
                text(tag, NAME_MAX, cur(c)->name);
            }
            if (!parse_struct_def(c, nested_union)) {
                return 0;
            }
            if (tag[0]) {
                inner = struct_find(c, tag);
            } else {
                inner = c->nstructs - 1;
            }
            if (inner < 0) {
                inner = c->nstructs - 1;
            }
            while (take(c, T_STAR)) {
                is_ptr = 1;
            }
            if (cur(c)->kind != T_ID) {
                StructDef *inn = &c->structs[inner];
                int fi;
                for (fi = 0; fi < inn->nfields; ++fi) {
                    if (!struct_add_field(c, s, inn->fname[fi], inn->is_float[fi],
                                          inn->fwidth[fi], is_union,
                                          cur(c)->line, cur(c)->column,
                                          inn->fstruct[fi], inn->fptr[fi])) {
                        return 0;
                    }
                }
                if (!expect(c, T_SEMI, "expected ; after field")) {
                    return 0;
                }
                continue;
            }
            fn = cur(c);
            if (!expect(c, T_ID, "expected field name")) {
                return 0;
            }
            if (take(c, T_LBRACK)) {
                if (!parse_array_dim(c, &count)) {
                    return 0;
                }
            }
            if (!expect(c, T_SEMI, "expected ; after field")) {
                return 0;
            }
            if (is_ptr) {
                fw = 8;
            } else {
                fw = (uint16_t)(c->structs[inner].size * (count > 0 ? count : 1));
            }
            if (!struct_add_field(c, s, fn->name, 0, fw, is_union, fn->line,
                                  fn->column, (int16_t)inner, (uint8_t)is_ptr)) {
                return 0;
            }
            continue;
        }
        fnbuf[0] = 0;
        if (!parse_type_n(c, &dt, fnbuf, NAME_MAX)) {
            return fail(c, cur(c)->line, cur(c)->column, "expected field type");
        }
        if (fnbuf[0]) {
            fn_store.kind = T_ID;
            fn_store.line = cur(c)->line;
            fn_store.column = cur(c)->column;
            fn_store.value = 0;
            text(fn_store.name, NAME_MAX, fnbuf);
            fn = &fn_store;
        } else {
            fn = cur(c);
            if (!expect(c, T_ID, "expected field name")) {
                return 0;
            }
        }
        if (take(c, T_COLON)) {
            int bits = 1;
            if (!parse_const_int(c, &bits) || bits < 1 || bits > 32) {
                return fail(c, fn->line, fn->column, "bad bitfield width");
            }
            c->field_bits = bits;
            dt.w = 4;
            dt.ptr = 0;
        }
            if (take(c, T_LBRACK)) {
                if (!parse_array_dim(c, &count)) {
                    return 0;
                }
            }
        nested = dt.sid;
        for (;;) {
            uint16_t thisw = dt.ptr ? 8u : dt.w;
            uint16_t elemw = 0;
            int thisc = count;
            if (!dt.ptr && dt.sid >= 0 && c->structs[dt.sid].size) {
                thisw = c->structs[dt.sid].size;
            }
            if (dt.arr_n > 1 && !dt.ptr) {
                elemw = dt.elemw ? dt.elemw : 1u;
                thisw = dt.w ? dt.w : thisw;
            }
            if (thisc > 1) {
                if (elemw == 0)
                    elemw = thisw ? thisw : 4u;
                else
                    elemw = thisw;
                thisw = (uint16_t)(thisw * (uint16_t)thisc);
            }
            if (!struct_add_field_ex(c, s, fn->name, dt.isf, thisw, elemw,
                                     is_union, fn->line, fn->column, nested,
                                     dt.ptr)) {
                return 0;
            }
            if (!take(c, T_COMMA)) {
                break;
            }
            count = 1;
            fn = cur(c);
            if (!expect(c, T_ID, "expected field name")) {
                return 0;
            }
            if (take(c, T_LBRACK)) {
                if (!parse_array_dim(c, &count)) {
                    return 0;
                }
            }
        }
        if (!expect(c, T_SEMI, "expected ; after field")) {
            return 0;
        }
    }
    if (skip_attr(c)) {
        s->packed = 1;
    }
    struct_recompute_layout(s);
    typedef_refresh_struct(c, (int)(s - c->structs));
    return 1;
}

static int parse_type_n(Compiler *c, DeclType *d, char *name_out, int ncap) {
    int got = 0;
    int is_u = 0;
    if (name_out && ncap > 0) {
        name_out[0] = 0;
    }
    d->w = 4;
    d->ptr = 0;
    d->isf = 0;
    d->uns = 0;
    d->is_void = 0;
    d->sid = -1;
    d->arr_n = 0;
    d->elemw = 0;
    skip_attr(c);
    while (take(c, T_CONST) || take(c, T_VOLATILE) || take(c, T_RESTRICT)) {
    }
    if (take(c, T_UNSIGNED)) {
        d->uns = 1;
        got = 1;
        d->w = 4;
    }
    if (take(c, T_SIGNED)) {
        got = 1;
        d->w = 4;
    }
    if (take(c, T_FLOAT) || take(c, T_COMPLEX)) {
        int was_complex = c->tokens[c->pos - 1].kind == T_COMPLEX;
        d->isf = 1;
        d->w = 4;
        if (was_complex || take(c, T_COMPLEX)) {
            d->isf = 0;
            d->w = 8;
            d->sid = (int16_t)ensure_complex(c);
        }
        got = 1;
    } else if (take(c, T_CHAR) || take(c, T_BOOL)) {
        d->w = 1;
        got = 1;
    } else if (take(c, T_SHORT)) {
        d->w = 2;
        take(c, T_INT);
        got = 1;
    } else if (take(c, T_LONG)) {
        d->w = 8;
        take(c, T_LONG);
        take(c, T_INT);
        got = 1;
    } else if (take(c, T_INT)) {
        d->w = 4;
        got = 1;
    } else if (take(c, T_VOID)) {
        d->w = 4;
        d->is_void = 1;
        got = 1;
    } else if (take(c, T_STRUCT) || take(c, T_UNION)) {
        char tag[NAME_MAX];
        int outer;
        tag[0] = 0;
        is_u = c->tokens[c->pos - 1].kind == T_UNION;
        if (cur(c)->kind == T_ID) {
            text(tag, NAME_MAX, cur(c)->name);
        }
        if (tag[0]) {
            outer = struct_find(c, tag);
        } else {
            outer = c->nstructs;
        }
        if (!parse_struct_def(c, is_u)) {
            return 0;
        }
        if (tag[0]) {
            d->sid = (int16_t)struct_find(c, tag);
        } else {
            d->sid = (int16_t)outer;
        }
        if (d->sid < 0) {
            d->sid = (int16_t)(c->nstructs - 1);
        }
        d->w = (d->sid >= 0 && c->structs[d->sid].size)
                   ? c->structs[d->sid].size
                   : 4u;
        if (d->w == 0) {
            d->w = 4;
        }
        got = 1;
    } else if (take(c, T_ENUM)) {
        if (cur(c)->kind == T_ID) {
            ++c->pos;
        }
        if (cur(c)->kind == T_LB) {
            ++c->pos;
            if (!parse_enum_list(c)) {
                return 0;
            }
        }
        d->w = 4;
        got = 1;
    }
    if (!got && cur(c)->kind == T_ID) {
        int ti = typedef_find(c, cur(c)->name);
        if (ti < 0) {
            return 0;
        }
        d->w = typedef_live_width(c, ti);
        d->ptr = (uint8_t)c->td_ptr[ti];
        d->sid = (int16_t)c->td_struct[ti];
        d->arr_n = (uint16_t)c->td_count[ti];
        d->elemw = (uint16_t)c->td_elemw[ti];
        ++c->pos;
        got = 1;
    }
    if (!got) {
        return 0;
    }
    {
        int stars = 0;
        while (take(c, T_STAR)) {
            stars++;
        }
        if (stars > 0) {
            d->ptr = 1;
            /* char* keeps element width; char** / int** pointee is a pointer. */
            if (stars > 1)
                d->w = 8;
            else if (d->arr_n > 1 && d->elemw)
                d->w = d->elemw;
            d->arr_n = 0;
            d->elemw = 0;
        }
    }
    if (cur(c)->kind == T_LP && c->pos + 1 < c->ntok &&
        c->tokens[c->pos + 1].kind == T_STAR) {
        take(c, T_LP);
        take(c, T_STAR);
        while (take(c, T_STAR) || take(c, T_CONST) || take(c, T_VOLATILE)) {
        }
        if (cur(c)->kind == T_ID) {
            if (name_out && ncap > 0) {
                text(name_out, (size_t)ncap, cur(c)->name);
            }
            ++c->pos;
        }
        if (take(c, T_LBRACK)) {
            int dummy = 1;
            if (!parse_array_dim(c, &dummy)) {
                return 0;
            }
        }
        if (!expect(c, T_RP, "expected ) in function pointer type")) {
            return 0;
        }
        if (cur(c)->kind == T_LP && !skip_paren_depth(c)) {
            return fail(c, cur(c)->line, cur(c)->column, "bad function pointer type");
        }
        d->ptr = 1;
        d->w = 8;
        d->is_void = 0;
        d->isf = 0;
    }
    skip_attr(c);
    return 1;
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

static int assignment_expr(Compiler *c);

static int expression(Compiler *c);

static int parse_cast_type(Compiler *c, CastType *ct);

static int primary(Compiler *c) {
    Token *t = cur(c);
    int id;
    int sym;
    int first;
    int count;
    int arg;

    if (take(c, T_GENERIC)) {
        int ctrl;
        int chosen = -1;
        int def = -1;
        int want_float = 0;
        if (!expect(c, T_LP, "expected ( after _Generic")) {
            return -1;
        }
        ctrl = assignment_expr(c);
        if (ctrl < 0 || !expect(c, T_COMMA, "expected comma in _Generic")) {
            return -1;
        }
        want_float = c->nodes[ctrl].is_float;
        while (!take(c, T_RP) && cur(c)->kind != T_EOF) {
            int is_def = 0;
            int assoc_f = 0;
            int e;
            if (take(c, T_DEFAULT)) {
                is_def = 1;
            } else {
                take(c, T_UNSIGNED);
                take(c, T_SIGNED);
                take(c, T_CONST);
                if (take(c, T_FLOAT)) {
                    assoc_f = 1;
                } else {
                    take(c, T_LONG);
                    take(c, T_SHORT);
                    take(c, T_INT);
                    take(c, T_CHAR);
                    take(c, T_BOOL);
                    take(c, T_VOID);
                    take(c, T_ID);
                }
                while (take(c, T_STAR)) {
                }
            }
            if (!expect(c, T_COLON, "expected : in _Generic")) {
                return -1;
            }
            e = assignment_expr(c);
            if (e < 0) {
                return -1;
            }
            if (is_def) {
                def = e;
            } else if (assoc_f == want_float && chosen < 0) {
                chosen = e;
            }
            take(c, T_COMMA);
        }
        if (chosen < 0) {
            chosen = def;
        }
        if (chosen < 0) {
            return fail(c, t->line, t->column, "_Generic has no match");
        }
        return chosen;
    }
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
    if (take(c, T_STR)) {
        id = node(c, N_STR, t);
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
        char m[80];
        int n = 0;
        const char *p = "expected expression k=";
        int kv = (int)t->kind;
        while (p[n]) {
            m[n] = p[n];
            n++;
        }
        if (kv >= 100 && n + 1 < 80) {
            m[n++] = (char)('0' + kv / 100);
        }
        if (kv >= 10 && n + 1 < 80) {
            m[n++] = (char)('0' + (kv / 10) % 10);
        }
        if (n + 1 < 80) {
            m[n++] = (char)('0' + kv % 10);
        }
        if (t->name[0] && n + 2 < 80) {
            int k = 0;
            m[n++] = ' ';
            while (t->name[k] && n + 1 < 80) {
                m[n++] = t->name[k++];
            }
        }
        m[n] = 0;
        fail(c, t->line, t->column, m);
        return -1;
    }
    ++c->pos;
    {
        int ci = const_find(c, t->name);
        if (ci >= 0) {
            id = node(c, N_INT, t);
            if (id >= 0) {
                c->nodes[id].value = c->const_val[ci];
            }
            return id;
        }
    }
    if (take(c, T_LP)) {
        id = node(c, N_CALL, t);
        if (id < 0) {
            return -1;
        }
        text(c->nodes[id].name, NAME_MAX, t->name);
        first = c->nargs;
        count = 0;
        if (same(t->name, "va_arg")) {
            arg = assignment_expr(c);
            if (arg < 0) {
                return -1;
            }
            c->args[c->nargs++] = arg;
            count = 1;
            if (!expect(c, T_COMMA, "expected comma in va_arg")) {
                return -1;
            }
            {
                CastType ct;
                if (!parse_cast_type(c, &ct)) {
                    take(c, T_ID);
                }
            }
            if (!expect(c, T_RP, "expected ) after va_arg")) {
                return -1;
            }
            c->nodes[id].left = first;
            c->nodes[id].value = count;
            return id;
        }
        /* Collect args locally so nested calls cannot interleave into this
         * call's contiguous args[] span (broke storeb(dst, loadb(src))). */
        {
            int local[CALL_ARG_LOCAL];
            int local_n = 0;
            int ai;
            if (!take(c, T_RP)) {
                do {
                    if (local_n == CALL_ARG_LOCAL) {
                        fail(c, t->line, t->column, "too many arguments");
                        return -1;
                    }
                    arg = assignment_expr(c);
                    if (arg < 0) {
                        return -1;
                    }
                    local[local_n++] = arg;
                } while (take(c, T_COMMA));
                if (!expect(c, T_RP, "expected ) after arguments")) {
                    return -1;
                }
            }
            if (c->nargs + local_n > ARG_MAX) {
                fail(c, t->line, t->column, "argument table full");
                return -1;
            }
            first = c->nargs;
            for (ai = 0; ai < local_n; ++ai) {
                c->args[c->nargs++] = local[ai];
            }
            count = local_n;
        }
        c->nodes[id].left = first;
        c->nodes[id].value = count;
        c->nodes[id].right = 0;
        {
            const Builtin *b = builtin(t->name);
            int fn;
            if (b && b->ret_float) {
                c->nodes[id].is_float = 1;
            } else {
                fn = func_find(c, t->name);
                if (fn >= 0) {
                    text(c->nodes[id].name, NAME_MAX, c->funcs[fn].name);
                    c->nodes[id].sid = c->funcs[fn].ret_sid;
                    if (c->funcs[fn].ret == 2) {
                        c->nodes[id].is_float = 1;
                    }
                } else if (fn < 0 && !b) {
                    int sy = sym_find(c, t->name);
                    if (sy >= 0) {
                        c->nodes[id].right = sy + 1;
                    }
                }
            }
        }
        return id;
    }
    sym = sym_find(c, t->name);
    if (sym < 0) {
        int fn = func_find(c, t->name);
        if (fn >= 0) {
            id = node(c, N_FNPTR, t);
            if (id >= 0) {
                c->nodes[id].value = fn;
            }
            return id;
        }
        fail(c, t->line, t->column, "unknown variable");
        return -1;
    }
    if (take(c, T_LBRACK)) {
        int idx = expression(c);
        int sid;
        if (idx < 0 || !expect(c, T_RBRACK, "expected ]")) {
            return -1;
        }
        id = node(c, N_INDEX, t);
        if (id >= 0) {
            c->nodes[id].value = sym;
            c->nodes[id].left = idx;
            c->nodes[id].is_float = c->syms[sym].is_float;
            c->nodes[id].sid = c->syms[sym].struct_id;
        }
        sid = c->syms[sym].struct_id;
        while (cur(c)->kind == T_DOT || cur(c)->kind == T_ARROW ||
               cur(c)->kind == T_LBRACK) {
            Token *fld;
            int fi;
            int next_arrow;
            int fid;
            uint8_t fw;
            uint8_t isf;
            if (take(c, T_LBRACK)) {
                int idx2 = expression(c);
                int stride = 4;
                if (idx2 < 0 || !expect(c, T_RBRACK, "expected ]")) {
                    return -1;
                }
                if (sid >= 0 && c->structs[sid].size) {
                    stride = (int)c->structs[sid].size;
                    if (stride < 1) {
                        stride = 4;
                    }
                }
                fid = node(c, N_PTRFIELD, t);
                if (fid >= 0) {
                    c->nodes[fid].left = id;
                    c->nodes[fid].right = idx2;
                    c->nodes[fid].third = 0;
                    c->nodes[fid].value = stride;
                    c->nodes[fid].is_float = 0;
                    c->nodes[fid].sid = (int16_t)sid;
                }
                id = fid;
                continue;
            }
            if (sid < 0) {
                fail(c, t->line, t->column, "not a struct");
                return -1;
            }
            next_arrow = (cur(c)->kind == T_ARROW);
            ++c->pos;
            fld = cur(c);
            if (!expect(c, T_ID, "expected field")) {
                return -1;
            }
            fi = field_find(&c->structs[sid], fld->name);
            if (fi < 0) {
                fail(c, fld->line, fld->column, "unknown field");
                return -1;
            }
            fw = (uint8_t)c->structs[sid].fwidth[fi];
            isf = c->structs[sid].is_float[fi];
            if (c->nodes[id].kind == N_INDEX && !next_arrow) {
                fid = node(c, N_FIELD, fld);
                if (fid >= 0) {
                    c->nodes[fid].value = sym;
                    c->nodes[fid].left = fi;
                    c->nodes[fid].right = idx;
                    c->nodes[fid].third = (int)(fw & 15);
                    c->nodes[fid].is_float = isf;
                }
                id = fid;
            } else {
                fid = node(c, N_PTRFIELD, fld);
                if (fid >= 0) {
                    c->nodes[fid].left = id;
                    c->nodes[fid].third = (int)c->structs[sid].foff[fi];
                    c->nodes[fid].value = fw;
                    if (c->structs[sid].felemw[fi] > 0) {
                        c->nodes[fid].value |= 0x40000;
                    }
                    c->nodes[fid].is_float = isf;
                    c->nodes[fid].sid = c->structs[sid].fstruct[fi];
                }
                id = fid;
            }
            sid = c->structs[sid].fstruct[fi];
        }
        return id;
    }
    id = node(c, N_VAR, t);
    if (id >= 0) {
        c->nodes[id].value = sym;
        c->nodes[id].is_float = c->syms[sym].is_float;
        c->nodes[id].sid = c->syms[sym].struct_id;
    }
    if (cur(c)->kind == T_DOT || cur(c)->kind == T_ARROW) {
        int sid = c->syms[sym].struct_id;
        int first_fi = -1;
        int prev_fi = -1;
        int extra = 0;
        int is_arrow = 0;
        uint8_t fw = 4;
        uint8_t isf = 0;
        int fid;
        if (sid < 0) {
            fail(c, t->line, t->column,
                 cur(c)->kind == T_ARROW ? "not a struct pointer" : "not a struct");
            return -1;
        }
        while (cur(c)->kind == T_DOT || cur(c)->kind == T_ARROW) {
            Token *fld;
            int next_arrow = (cur(c)->kind == T_ARROW);
            if (first_fi >= 0 && prev_fi >= 0 && sid >= 0 &&
                c->structs[sid].fptr[prev_fi]) {
                break;
            }
            ++c->pos;
            if (first_fi < 0) {
                is_arrow = next_arrow;
            } else {
                sid = c->structs[sid].fstruct[prev_fi];
                if (sid < 0) {
                    fail(c, t->line, t->column, "not a struct");
                    return -1;
                }
            }
            fld = cur(c);
            if (!expect(c, T_ID, "expected field")) {
                return -1;
            }
            prev_fi = field_find(&c->structs[sid], fld->name);
            if (prev_fi < 0) {
                fail(c, fld->line, fld->column, "unknown field");
                return -1;
            }
            fw = (uint8_t)c->structs[sid].fwidth[prev_fi];
            isf = c->structs[sid].is_float[prev_fi];
            if (first_fi < 0) {
                first_fi = prev_fi;
            } else {
                extra += c->structs[sid].foff[prev_fi];
            }
        }
        fid = node(c, is_arrow ? N_ARROW : N_FIELD, t);
        if (fid >= 0) {
            c->nodes[fid].value = sym;
            c->nodes[fid].left = first_fi;
            c->nodes[fid].right = -1;
            c->nodes[fid].third = (extra << 4) | (int)(fw & 15);
            c->nodes[fid].is_float = isf;
            if (prev_fi >= 0 && sid >= 0) {
                c->nodes[fid].sid = c->structs[sid].fstruct[prev_fi];
            }
        }
        {
            /* Keep array element width for subsequent [index] on this field. */
            int arr_elem = 0;
            int arr_sid = c->syms[sym].struct_id;
            if (first_fi >= 0 && arr_sid >= 0 &&
                first_fi < c->structs[arr_sid].nfields)
                arr_elem = (int)c->structs[arr_sid].felemw[first_fi];
            if (prev_fi >= 0 && sid >= 0) {
                sid = c->structs[sid].fstruct[prev_fi];
            }
            while (fid >= 0 && (cur(c)->kind == T_LBRACK || cur(c)->kind == T_DOT ||
                                cur(c)->kind == T_ARROW)) {
                int p;
                if (take(c, T_LBRACK)) {
                    int idxn = expression(c);
                    int stride = 4;
                    if (idxn < 0 || !expect(c, T_RBRACK, "expected ]")) {
                        return -1;
                    }
                    if (arr_elem > 0)
                        stride = arr_elem;
                    else if (sid >= 0 && c->structs[sid].size) {
                        stride = (int)c->structs[sid].size;
                        if (stride < 1) {
                            stride = 4;
                        }
                    }
                    p = node(c, N_PTRFIELD, t);
                    if (p < 0) {
                        return -1;
                    }
                    c->nodes[p].left = fid;
                    c->nodes[p].right = idxn;
                    c->nodes[p].third = 0;
                    c->nodes[p].value = stride;
                    c->nodes[p].sid = (int16_t)sid;
                    /* `p->defaults[i]`: N_ARROW yields the field address; load
                     * the pointer before scaling the index. */
                    if (arr_elem == 0 &&
                        (c->nodes[fid].kind == N_ARROW ||
                         c->nodes[fid].kind == N_FIELD ||
                         (c->nodes[fid].kind == N_PTRFIELD &&
                          c->nodes[fid].right < 0))) {
                        c->nodes[p].value |= 0x10000;
                    }
                    /* Mark byte elements so stores/loads use STOREB/LOADB. */
                    if (stride == 1)
                        c->nodes[p].value |= 0x20000;
                    fid = p;
                    arr_elem = 0;
                    continue;
                }
                {
                    Token *fld;
                    int fi;
                    int next_arrow = (cur(c)->kind == T_ARROW);
                    ++c->pos;
                    (void)next_arrow;
                    if (sid < 0) {
                        fail(c, t->line, t->column, "not a struct");
                        return -1;
                    }
                    fld = cur(c);
                    if (!expect(c, T_ID, "expected field")) {
                        return -1;
                    }
                    fi = field_find(&c->structs[sid], fld->name);
                    if (fi < 0) {
                        fail(c, fld->line, fld->column, "unknown field");
                        return -1;
                    }
                    p = node(c, N_PTRFIELD, fld);
                    if (p < 0) {
                        return -1;
                    }
                    c->nodes[p].left = fid;
                    c->nodes[p].right = -1;
                    c->nodes[p].third = (int)c->structs[sid].foff[fi];
                    c->nodes[p].value = (int)c->structs[sid].fwidth[fi];
                    if (c->structs[sid].felemw[fi] > 0) {
                        c->nodes[p].value |= 0x40000;
                    }
                    if (next_arrow && c->nodes[fid].kind == N_PTRFIELD &&
                        c->nodes[fid].right >= 0) {
                        c->nodes[p].value |= 0x10000;
                    }
                    c->nodes[p].is_float = c->structs[sid].is_float[fi];
                    c->nodes[p].sid = c->structs[sid].fstruct[fi];
                    fid = p;
                    sid = c->structs[sid].fstruct[fi];
                }
            }
        }
        if (fid >= 0 && take(c, T_LP)) {
            int calln = node(c, N_CALL, t);
            int first;
            int count = 0;
            int argn;
            int local[CALL_ARG_LOCAL];
            int local_n = 0;
            int ai;
            if (calln < 0) {
                return -1;
            }
            if (!take(c, T_RP)) {
                do {
                    if (local_n == CALL_ARG_LOCAL) {
                        fail(c, t->line, t->column, "too many arguments");
                        return -1;
                    }
                    argn = assignment_expr(c);
                    if (argn < 0) {
                        return -1;
                    }
                    local[local_n++] = argn;
                } while (take(c, T_COMMA));
                if (!expect(c, T_RP, "expected ) after arguments")) {
                    return -1;
                }
            }
            if (c->nargs + local_n > ARG_MAX) {
                fail(c, t->line, t->column, "argument table full");
                return -1;
            }
            first = c->nargs;
            for (ai = 0; ai < local_n; ++ai) {
                c->args[c->nargs++] = local[ai];
            }
            count = local_n;
            c->nodes[calln].left = first;
            c->nodes[calln].value = count;
            c->nodes[calln].right = -2;
            c->nodes[calln].third = fid;
            c->nodes[calln].name[0] = 0;
            return calln;
        }
        return fid;
    }
    return id;
}

static int is_typename_at(Compiler *c, int pos) {
    Token *t;
    if (pos < 0 || pos >= c->ntok) {
        return 0;
    }
    t = &c->tokens[pos];
    switch (t->kind) {
    case T_INT:
    case T_FLOAT:
    case T_CHAR:
    case T_LONG:
    case T_SHORT:
    case T_VOID:
    case T_UNSIGNED:
    case T_SIGNED:
    case T_BOOL:
    case T_CONST:
    case T_VOLATILE:
    case T_RESTRICT:
    case T_STRUCT:
    case T_UNION:
    case T_ENUM:
        return 1;
    case T_ID:
        return typedef_find(c, t->name) >= 0;
    default:
        return 0;
    }
}

static int32_t pack_cast(const CastType *ct) {
    int32_t v = (int32_t)ct->width;
    if (ct->is_uns) {
        v |= CAST_UNS;
    }
    if (ct->is_ptr) {
        v |= CAST_PTR;
    }
    if (ct->is_bool) {
        v |= CAST_BOOL;
    }
    if (ct->is_void) {
        v |= CAST_VOID;
    }
    v |= ((int32_t)ct->pointee) << 16;
    if (ct->pointee_float) {
        v |= CAST_PFL;
    }
    return v;
}

static int skip_paren_depth(Compiler *c) {
    int d = 0;
    if (!take(c, T_LP)) {
        return 0;
    }
    d = 1;
    while (d > 0 && cur(c)->kind != T_EOF) {
        if (cur(c)->kind == T_LP) {
            d++;
        } else if (cur(c)->kind == T_RP) {
            d--;
        }
        ++c->pos;
    }
    return d == 0;
}

static int parse_cast_type(Compiler *c, CastType *ct) {
    int got = 0;
    int tagged = 0;
    int saw_sign = 0;
    ct->width = 4;
    ct->is_float = 0;
    ct->is_uns = 0;
    ct->is_ptr = 0;
    ct->is_void = 0;
    ct->is_bool = 0;
    ct->pointee = 0;
    ct->pointee_float = 0;
    ct->sid = -1;
    for (;;) {
        if (take(c, T_CONST) || take(c, T_VOLATILE) || take(c, T_RESTRICT)) {
            continue;
        }
        if (take(c, T_UNSIGNED)) {
            ct->is_uns = 1;
            saw_sign = 1;
            continue;
        }
        if (take(c, T_SIGNED)) {
            ct->is_uns = 0;
            saw_sign = 1;
            continue;
        }
        break;
    }
    if (take(c, T_BOOL)) {
        ct->width = 1;
        ct->is_bool = 1;
        got = 1;
    } else if (take(c, T_CHAR)) {
        ct->width = 1;
        got = 1;
    } else if (take(c, T_SHORT)) {
        ct->width = 2;
        take(c, T_INT);
        got = 1;
    } else if (take(c, T_LONG)) {
        ct->width = 8;
        take(c, T_LONG);
        take(c, T_INT);
        got = 1;
    } else if (take(c, T_INT)) {
        ct->width = 4;
        got = 1;
    } else if (take(c, T_FLOAT) || take(c, T_COMPLEX)) {
        ct->is_float = 1;
        ct->width = 4;
        got = 1;
    } else if (take(c, T_VOID)) {
        ct->is_void = 1;
        ct->width = 0;
        got = 1;
    } else if (take(c, T_STRUCT) || take(c, T_UNION)) {
        Token *tn = cur(c);
        int sid;
        if (!expect(c, T_ID, "expected struct name in cast")) {
            return 0;
        }
        sid = struct_find(c, tn->name);
        ct->width = (sid >= 0 && c->structs[sid].size) ? (uint8_t)c->structs[sid].size : 4u;
        ct->sid = (int16_t)sid;
        tagged = 1;
        got = 1;
    } else if (take(c, T_ENUM)) {
        take(c, T_ID);
        ct->width = 4;
        got = 1;
    } else if (cur(c)->kind == T_ID) {
        int ti = typedef_find(c, cur(c)->name);
        if (ti < 0) {
            return fail(c, cur(c)->line, cur(c)->column, "expected type in cast");
        }
        ct->width = (uint8_t)typedef_live_width(c, ti);
        ct->sid = (int16_t)c->td_struct[ti];
        if (c->td_ptr[ti]) {
            ct->is_ptr = 1;
            ct->pointee = 4;
            ct->width = 8;
        }
        ++c->pos;
        got = 1;
    }
    if (!got && saw_sign) {
        got = 1;
        ct->width = 4;
    }
    if (!got) {
        return fail(c, cur(c)->line, cur(c)->column, "expected type in cast");
    }
    while (take(c, T_CONST) || take(c, T_VOLATILE) || take(c, T_RESTRICT)) {
    }
    while (take(c, T_STAR)) {
        if (ct->is_ptr) {
            ct->pointee = 8;
            ct->pointee_float = 0;
        } else {
            ct->pointee = ct->is_float ? 4u : (ct->is_void ? 1u : (ct->width ? ct->width : 4u));
            ct->pointee_float = ct->is_float;
        }
        ct->is_ptr = 1;
        ct->is_float = 0;
        ct->is_void = 0;
        ct->is_bool = 0;
        tagged = 0;
        ct->width = 8;
        while (take(c, T_CONST) || take(c, T_VOLATILE) || take(c, T_RESTRICT)) {
        }
    }
    if (cur(c)->kind == T_LP && c->pos + 1 < c->ntok &&
        c->tokens[c->pos + 1].kind == T_STAR) {
        take(c, T_LP);
        take(c, T_STAR);
        while (take(c, T_STAR) || take(c, T_CONST) || take(c, T_VOLATILE)) {
        }
        if (!expect(c, T_RP, "expected ) in function pointer cast")) {
            return 0;
        }
        if (cur(c)->kind == T_LP && !skip_paren_depth(c)) {
            return fail(c, cur(c)->line, cur(c)->column, "bad function pointer cast");
        }
        if (!ct->is_ptr) {
            ct->pointee = 8;
        }
        ct->is_ptr = 1;
        ct->width = 8;
        ct->is_float = 0;
        ct->is_void = 0;
        tagged = 0;
    }
    if (tagged && !ct->is_ptr) {
        return fail(c, cur(c)->line, cur(c)->column, "cannot cast to struct");
    }
    return 1;
}

static int postfix_expr(Compiler *c, int id) {
    while (id >= 0) {
        Token *t = cur(c);
        if (take(c, T_LBRACK)) {
            int idx = expression(c);
            int p;
            int stride = 4;
            if (idx < 0 || !expect(c, T_RBRACK, "expected ]")) {
                return -1;
            }
            if (c->nodes[id].kind == N_CAST) {
                int pw = CAST_POINTEE(c->nodes[id].value);
                if (pw > 0) {
                    stride = pw;
                }
            } else if (c->nodes[id].kind == N_VAR) {
                int sy = c->nodes[id].value;
                if (c->syms[sy].is_ptr && c->syms[sy].pointee) {
                    stride = (int)c->syms[sy].pointee;
                } else if (c->syms[sy].stride) {
                    stride = (int)c->syms[sy].stride;
                }
                if (c->syms[sy].struct_id >= 0 &&
                    c->structs[c->syms[sy].struct_id].size) {
                    stride = (int)c->structs[c->syms[sy].struct_id].size;
                }
            } else if (c->nodes[id].kind == N_PTRFIELD ||
                       c->nodes[id].kind == N_FIELD ||
                       c->nodes[id].kind == N_ARROW) {
                int psid = c->nodes[id].sid;
                if (psid >= 0 && psid < c->nstructs &&
                    c->structs[psid].size) {
                    stride = (int)c->structs[psid].size;
                } else if (c->nodes[id].kind == N_PTRFIELD &&
                           c->nodes[id].value) {
                    int fw = c->nodes[id].value & 0xffff;
                    stride = (fw == 8) ? 1 : (fw ? fw : 4);
                }
            }
            p = node(c, N_PTRFIELD, t);
            if (p < 0) {
                return -1;
            }
            c->nodes[p].left = id;
            c->nodes[p].right = idx;
            c->nodes[p].third = 0;
            c->nodes[p].value = stride;
            /* Pointer field (`p->defaults[i]`): load the pointer, then index. */
            if (c->nodes[id].kind == N_PTRFIELD && c->nodes[id].right < 0) {
                c->nodes[p].value |= 0x10000;
            }
            c->nodes[p].sid = c->nodes[id].sid;
            id = p;
            continue;
        }
        if (cur(c)->kind == T_ARROW || cur(c)->kind == T_DOT) {
            Token *fld;
            int is_arrow = (cur(c)->kind == T_ARROW);
            int sid = c->nodes[id].sid;
            int fi;
            int p;
            ++c->pos;
            if (sid < 0 && (c->nodes[id].kind == N_VAR ||
                            c->nodes[id].kind == N_ARROW ||
                            c->nodes[id].kind == N_FIELD)) {
                sid = c->syms[c->nodes[id].value].struct_id;
            }
            fld = cur(c);
            if (!expect(c, T_ID, "expected field")) {
                return -1;
            }
            if (sid < 0) {
                fail(c, fld->line, fld->column, "not a struct");
                return -1;
            }
            fi = field_find(&c->structs[sid], fld->name);
            if (fi < 0) {
                fail(c, fld->line, fld->column, "unknown field");
                return -1;
            }
            p = node(c, N_PTRFIELD, fld);
            if (p < 0) {
                return -1;
            }
            c->nodes[p].left = id;
            c->nodes[p].right = -1;
            c->nodes[p].third = (int)c->structs[sid].foff[fi];
            c->nodes[p].value = (int)c->structs[sid].fwidth[fi];
            if (c->structs[sid].felemw[fi] > 0) {
                c->nodes[p].value |= 0x40000;
            }
            if (is_arrow && c->nodes[id].kind == N_PTRFIELD &&
                c->nodes[id].right >= 0) {
                c->nodes[p].value |= 0x10000;
            }
            c->nodes[p].is_float = c->structs[sid].is_float[fi];
            c->nodes[p].sid = c->structs[sid].fstruct[fi];
            id = p;
            continue;
        }
        if (take(c, T_PLUSPLUS) || take(c, T_MINUSMINUS)) {
            int dec = c->tokens[c->pos - 1].kind == T_MINUSMINUS;
            int p = node(c, dec ? N_POSTDEC : N_POSTINC, t);
            if (p < 0) {
                return -1;
            }
            c->nodes[p].left = id;
            if (c->nodes[id].kind == N_VAR) {
                c->nodes[p].value = c->nodes[id].value;
            }
            id = p;
            continue;
        }
        if (take(c, T_LP)) {
            int calln = node(c, N_CALL, t);
            int first;
            int count = 0;
            int argn;
            int local[CALL_ARG_LOCAL];
            int local_n = 0;
            int ai;
            if (calln < 0) {
                return -1;
            }
            if (!take(c, T_RP)) {
                do {
                    if (local_n == CALL_ARG_LOCAL) {
                        fail(c, t->line, t->column, "too many arguments");
                        return -1;
                    }
                    argn = assignment_expr(c);
                    if (argn < 0) {
                        return -1;
                    }
                    local[local_n++] = argn;
                } while (take(c, T_COMMA));
                if (!expect(c, T_RP, "expected ) after arguments")) {
                    return -1;
                }
            }
            if (c->nargs + local_n > ARG_MAX) {
                fail(c, t->line, t->column, "argument table full");
                return -1;
            }
            first = c->nargs;
            for (ai = 0; ai < local_n; ++ai) {
                c->args[c->nargs++] = local[ai];
            }
            count = local_n;
            c->nodes[calln].left = first;
            c->nodes[calln].value = count;
            c->nodes[calln].right = -2;
            c->nodes[calln].third = id;
            c->nodes[calln].name[0] = 0;
            id = calln;
            continue;
        }
        break;
    }
    return id;
}

static int sizeof_expr_node(Compiler *c, int v) {
    Node *n;
    if (v < 0)
        return 4;
    n = &c->nodes[v];
    if (n->kind == N_VAR) {
        Symbol *s = &c->syms[n->value];
        if (s->is_array) {
            int cnt = s->array_n ? (int)s->array_n : 1;
            if (s->struct_id >= 0)
                return (int)c->structs[s->struct_id].size * cnt;
            return (int)(s->stride ? s->stride : 4) * cnt;
        }
        if (s->is_ptr)
            return 8;
        if (s->struct_id >= 0)
            return (int)c->structs[s->struct_id].size;
        if (s->is_float)
            return 4;
        return s->width ? (int)s->width : 4;
    }
    if (n->kind == N_DEREF) {
        if (n->sid >= 0 && n->sid < c->nstructs && c->structs[n->sid].size)
            return (int)c->structs[n->sid].size;
        if (n->left >= 0 && c->nodes[n->left].kind == N_VAR) {
            Symbol *s = &c->syms[c->nodes[n->left].value];
            if (s->struct_id >= 0)
                return (int)c->structs[s->struct_id].size;
            if (s->is_ptr && s->pointee)
                return s->pointee < 4 ? 4 : (int)s->pointee;
        }
        return 4;
    }
    if (n->kind == N_FIELD || n->kind == N_ARROW || n->kind == N_PTRFIELD) {
        if (n->sid >= 0 && n->sid < c->nstructs && c->structs[n->sid].size)
            return (int)c->structs[n->sid].size;
        if (n->value > 0)
            return (int)n->value;
        return n->is_float ? 4 : 4;
    }
    if (n->kind == N_INDEX) {
        if (n->sid >= 0 && n->sid < c->nstructs && c->structs[n->sid].size)
            return (int)c->structs[n->sid].size;
        if (n->value >= 0 && n->value < c->nsyms) {
            Symbol *s = &c->syms[n->value];
            if (s->struct_id >= 0)
                return (int)c->structs[s->struct_id].size;
            if (s->is_float)
                return 4;
            if (s->stride)
                return (int)s->stride;
            return s->width ? (int)s->width : 4;
        }
        return 4;
    }
    if (n->kind == N_ADDR || n->kind == N_FNPTR)
        return 8;
    if (n->sid >= 0 && n->sid < c->nstructs && c->structs[n->sid].size)
        return (int)c->structs[n->sid].size;
    if (n->is_float)
        return 4;
    return 4;
}

/* Determine whether dereferencing operand `v` yields a float value, i.e.
 * whether its pointee type is float. Mirrors the pointer-classification used
 * by the N_DEREF codegen: unwrap pointer casts (CAST_PFL marks float pointee),
 * consult the symbol for a plain pointer variable (its is_float records the
 * pointee's float-ness), follow &lvalue, else fall back to the node's own
 * float flag (fields/index results already carry pointee float-ness). */
static int deref_pointee_float(Compiler *c, int v) {
    while (v >= 0 && c->nodes[v].kind == N_CAST) {
        int32_t cv = c->nodes[v].value;
        if (cv & CAST_PTR)
            return (cv & CAST_PFL) ? 1 : 0;
        v = c->nodes[v].left;
    }
    if (v < 0)
        return 0;
    if (c->nodes[v].kind == N_VAR) {
        Symbol *s = &c->syms[c->nodes[v].value];
        return (s->is_ptr && s->is_float) ? 1 : 0;
    }
    if (c->nodes[v].kind == N_ADDR)
        return c->nodes[v].left >= 0 ? c->nodes[c->nodes[v].left].is_float : 0;
    return c->nodes[v].is_float;
}

static int unary(Compiler *c) {
    Token *t = cur(c);
    int id;
    int v;

    if (take(c, T_AMP)) {
        v = unary(c);
        if (v < 0) {
            return -1;
        }
        id = node(c, N_ADDR, t);
        if (id >= 0) {
            c->nodes[id].left = v;
        }
        return id;
    }
    if (take(c, T_STAR)) {
        v = unary(c);
        if (v < 0) {
            return -1;
        }
        id = node(c, N_DEREF, t);
        if (id >= 0) {
            c->nodes[id].left = v;
            c->nodes[id].sid = c->nodes[v].sid;
            if (c->nodes[id].sid < 0 && c->nodes[v].kind == N_VAR) {
                c->nodes[id].sid = c->syms[c->nodes[v].value].struct_id;
            }
            /* A dereferenced float pointer yields a float value; without this
             * the deref was treated as int and mixed float/int arithmetic
             * inserted a spurious ITOF that reinterpreted the float bits. */
            c->nodes[id].is_float = (uint8_t)deref_pointee_float(c, v);
        }
        return postfix_expr(c, id);
    }
    if (take(c, T_TILDE)) {
        v = unary(c);
        if (v < 0) {
            return -1;
        }
        id = node(c, N_BITNOT, t);
        if (id >= 0) {
            c->nodes[id].left = v;
        }
        return id;
    }
    if (take(c, T_PLUSPLUS)) {
        v = unary(c);
        if (v < 0) {
            return -1;
        }
        id = node(c, N_PREINC, t);
        if (id >= 0) {
            c->nodes[id].left = v;
        }
        return id;
    }
    if (take(c, T_MINUSMINUS)) {
        v = unary(c);
        if (v < 0) {
            return -1;
        }
        id = node(c, N_PREDEC, t);
        if (id >= 0) {
            c->nodes[id].left = v;
        }
        return id;
    }
    if (take(c, T_SIZEOF) || take(c, T_ALIGNOF)) {
        int sz = 4;
        int align = 0;
        align = (t->kind == T_ALIGNOF);
        if (cur(c)->kind == T_LP && is_typename_at(c, c->pos + 1)) {
            CastType ct;
            take(c, T_LP);
            if (!parse_cast_type(c, &ct) ||
                !expect(c, T_RP, "expected ) after sizeof")) {
                return -1;
            }
            if (ct.is_ptr) {
                sz = 8;
            } else if (ct.is_void) {
                sz = 1;
            } else if (ct.is_float) {
                sz = 4;
            } else if (ct.sid >= 0 && ct.sid < c->nstructs &&
                       c->structs[ct.sid].size) {
                sz = (int)c->structs[ct.sid].size;
            } else {
                sz = ct.width ? (int)ct.width : 4;
            }
        } else if (take(c, T_LP)) {
            v = expression(c);
            if (v < 0 || !expect(c, T_RP, "expected ) after sizeof")) {
                return -1;
            }
            sz = sizeof_expr_node(c, v);
        } else {
            v = unary(c);
            if (v < 0) {
                return -1;
            }
            sz = sizeof_expr_node(c, v);
        }
        (void)align;
        if (align) {
            if (sz >= 8) {
                sz = 8;
            } else if (sz >= 4) {
                sz = 4;
            } else if (sz >= 2) {
                sz = 2;
            } else {
                sz = 1;
            }
        }
        id = node(c, N_INT, t);
        if (id >= 0) {
            c->nodes[id].value = sz;
        }
        return id;
    }
    if (cur(c)->kind == T_LP && is_typename_at(c, c->pos + 1)) {
        CastType ct;
        take(c, T_LP);
        if (!parse_cast_type(c, &ct) ||
            !expect(c, T_RP, "expected ) after cast")) {
            return -1;
        }
        if (take(c, T_LB)) {
            v = assignment_expr(c);
            while (!take(c, T_RB) && cur(c)->kind != T_EOF) {
                take(c, T_COMMA);
                if (cur(c)->kind == T_RB) {
                    break;
                }
                if (assignment_expr(c) < 0) {
                    return -1;
                }
            }
            if (v < 0) {
                return -1;
            }
            if (!ct.is_float && !ct.is_void) {
                id = node(c, N_CAST, t);
                if (id >= 0) {
                    c->nodes[id].left = v;
                    c->nodes[id].value = pack_cast(&ct);
                    c->nodes[id].is_float = 0;
                    c->nodes[id].sid = ct.sid;
                }
                return id;
            }
            if (ct.is_float) {
                id = node(c, N_CAST, t);
                if (id >= 0) {
                    c->nodes[id].left = v;
                    c->nodes[id].value = pack_cast(&ct);
                    c->nodes[id].is_float = 1;
                    c->nodes[id].sid = ct.sid;
                }
                return id;
            }
            return v;
        }
        v = unary(c);
        if (v < 0) {
            return -1;
        }
        id = node(c, N_CAST, t);
        if (id >= 0) {
            c->nodes[id].left = v;
            c->nodes[id].value = pack_cast(&ct);
            c->nodes[id].is_float = ct.is_float;
            c->nodes[id].sid = ct.sid;
        }
        return postfix_expr(c, id);
    }
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
    if (take(c, T_PLUS)) {
        return unary(c);
    }
    if (take(c, T_NOT)) {
        v = unary(c);
        if (v < 0) {
            return -1;
        }
        id = node(c, N_NOT, t);
        if (id >= 0) {
            c->nodes[id].left = v;
        }
        return id;
    }
    id = primary(c);
    if (id < 0) {
        return -1;
    }
    return postfix_expr(c, id);
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
        {
            int sid = c->nodes[left].sid;
            if (sid < 0 && (c->nodes[left].kind == N_VAR ||
                            c->nodes[left].kind == N_ARROW ||
                            c->nodes[left].kind == N_FIELD)) {
                sid = c->syms[c->nodes[left].value].struct_id;
            }
            if (sid < 0) {
                sid = c->nodes[right].sid;
            }
            if (sid < 0 && (c->nodes[right].kind == N_VAR ||
                            c->nodes[right].kind == N_ARROW ||
                            c->nodes[right].kind == N_FIELD)) {
                sid = c->syms[c->nodes[right].value].struct_id;
            }
            c->nodes[id].sid = (int16_t)sid;
        }
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

static int bit_shift(Compiler *c) {
    static const TokenKind k[] = {T_LSH, T_RSH};
    static const NodeKind n[] = {N_SHL, N_SHR};
    return binary(c, sum, k, n, 2);
}

static int relation(Compiler *c) {
    static const TokenKind k[] = {T_LT, T_LE, T_GT, T_GE};
    static const NodeKind n[] = {N_LT, N_LE, N_GT, N_GE};
    return binary(c, bit_shift, k, n, 4);
}

static int equality(Compiler *c) {
    static const TokenKind k[] = {T_EQ, T_NE};
    static const NodeKind n[] = {N_EQ, N_NE};
    return binary(c, relation, k, n, 2);
}

static int bit_and(Compiler *c) {
    static const TokenKind k[] = {T_AMP};
    static const NodeKind n[] = {N_BITAND};
    return binary(c, equality, k, n, 1);
}

static int bit_xor(Compiler *c) {
    static const TokenKind k[] = {T_CARET};
    static const NodeKind n[] = {N_BITXOR};
    return binary(c, bit_and, k, n, 1);
}

static int bit_or(Compiler *c) {
    static const TokenKind k[] = {T_PIPE};
    static const NodeKind n[] = {N_BITOR};
    return binary(c, bit_xor, k, n, 1);
}

static int logical_and(Compiler *c) {
    static const TokenKind k[] = {T_ANDAND};
    static const NodeKind n[] = {N_AND};
    return binary(c, bit_or, k, n, 1);
}

static int logical_or(Compiler *c) {
    static const TokenKind k[] = {T_OROR};
    static const NodeKind n[] = {N_OR};
    return binary(c, logical_and, k, n, 1);
}

/* Float-ness of a node's *value* (not its pointee). A pointer variable holds
 * an integer address, so pointer arithmetic stays integer; a dereferenced
 * float pointer or a float scalar/field yields a float. Used by compound
 * assignment, which builds its binary node by hand and must classify operands
 * the same way the normal binary() path does. */
static int value_is_float(Compiler *c, int id) {
    if (id < 0)
        return 0;
    if (c->nodes[id].kind == N_VAR) {
        Symbol *s = &c->syms[c->nodes[id].value];
        return (s->is_float && !s->is_ptr) ? 1 : 0;
    }
    return c->nodes[id].is_float ? 1 : 0;
}

static int make_assign(Compiler *c, int lhs, int rhs, Token *t) {
    int id;
    if (lhs < 0 || rhs < 0) {
        return -1;
    }
    if (c->nodes[lhs].kind == N_VAR) {
        if (c->syms[c->nodes[lhs].value].is_const) {
            return fail(c, t->line, t->column, "assignment to const");
        }
        id = node(c, N_ASSIGN, t);
        if (id < 0) {
            return -1;
        }
        c->nodes[id].value = c->nodes[lhs].value;
        c->nodes[id].left = rhs;
        return id;
    }
    if (c->nodes[lhs].kind == N_DEREF) {
        id = node(c, N_DEREF_ASSIGN, t);
        if (id < 0) {
            return -1;
        }
        c->nodes[id].left = rhs;
        c->nodes[id].right = c->nodes[lhs].left;
        return id;
    }
    if (c->nodes[lhs].kind == N_INDEX) {
        id = node(c, N_INDEX_ASSIGN, t);
        if (id < 0) {
            return -1;
        }
        c->nodes[id].value = c->nodes[lhs].value;
        c->nodes[id].left = rhs;
        c->nodes[id].right = c->nodes[lhs].left;
        return id;
    }
    if (c->nodes[lhs].kind == N_FIELD) {
        int extra = c->nodes[lhs].third >> 4;
        int fw = c->nodes[lhs].third & 15;
        int fi = c->nodes[lhs].left;
        id = node(c, N_FIELD_ASSIGN, t);
        if (id < 0) {
            return -1;
        }
        c->nodes[id].value = c->nodes[lhs].value;
        c->nodes[id].left = rhs;
        c->nodes[id].right = c->nodes[lhs].right;
        if (c->nodes[id].right == 0) {
            c->nodes[id].right = -1;
        }
        if (fw == 0) {
            fw = 4;
        }
        c->nodes[id].third = fi | (fw << 8) | (extra << 16);
        c->nodes[id].is_float = c->nodes[lhs].is_float;
        return id;
    }
    if (c->nodes[lhs].kind == N_ARROW) {
        int extra = c->nodes[lhs].third >> 4;
        int fw = c->nodes[lhs].third & 15;
        int fi = c->nodes[lhs].left;
        id = node(c, N_FIELD_ASSIGN, t);
        if (id < 0) {
            return -1;
        }
        c->nodes[id].value = c->nodes[lhs].value;
        c->nodes[id].left = rhs;
        c->nodes[id].right = -3;
        if (fw == 0) {
            fw = 4;
        }
        c->nodes[id].third = fi | (fw << 8) | (extra << 16);
        c->nodes[id].is_float = c->nodes[lhs].is_float;
        return id;
    }
    if (c->nodes[lhs].kind == N_PTRFIELD) {
        int addr = c->nodes[lhs].left;
        int stride_v = c->nodes[lhs].value & 0xffff;
        int is_byte = (stride_v == 1) || ((c->nodes[lhs].value & 0x20000) != 0);
        int store_w = is_byte ? 1 : (stride_v == 2 ? 2 : (stride_v >= 8 ? 8 : 4));
        if (c->nodes[lhs].right >= 0) {
            int muln = node(c, N_MUL, t);
            int stride = node(c, N_INT, t);
            int addn;
            if (muln < 0 || stride < 0) {
                return -1;
            }
            c->nodes[stride].value = stride_v ? stride_v : 4;
            c->nodes[muln].left = c->nodes[lhs].right;
            c->nodes[muln].right = stride;
            addn = node(c, N_ADD, t);
            if (addn < 0) {
                return -1;
            }
            c->nodes[addn].left = addr;
            c->nodes[addn].right = muln;
            addr = addn;
        }
        if (c->nodes[lhs].third != 0) {
            int offn = node(c, N_INT, t);
            int addn;
            if (offn < 0) {
                return -1;
            }
            c->nodes[offn].value = c->nodes[lhs].third;
            addn = node(c, N_ADD, t);
            if (addn < 0) {
                return -1;
            }
            c->nodes[addn].left = addr;
            c->nodes[addn].right = offn;
            addr = addn;
        }
        id = node(c, N_DEREF_ASSIGN, t);
        if (id < 0) {
            return -1;
        }
        c->nodes[id].left = rhs;
        c->nodes[id].right = addr;
        c->nodes[id].value = store_w;
        return id;
    }
    return fail(c, t->line, t->column, "assignment needs lvalue");
}

static int assignment_expr(Compiler *c) {
    int left = logical_or(c);
    Token *t;
    int mid;
    int right;
    int id;
    if (left < 0) {
        return -1;
    }
    if (take(c, T_QUESTION)) {
        t = cur(c);
        mid = assignment_expr(c);
        if (mid < 0 || !expect(c, T_COLON, "expected : in ternary")) {
            return -1;
        }
        right = assignment_expr(c);
        if (right < 0) {
            return -1;
        }
        id = node(c, N_TERNARY, t);
        if (id < 0) {
            return -1;
        }
        c->nodes[id].left = left;
        c->nodes[id].right = mid;
        c->nodes[id].third = right;
        return id;
    }
    if (take(c, T_ASSIGN) || take(c, T_PLUSEQ) || take(c, T_MINUSEQ) ||
        take(c, T_STAREQ) || take(c, T_SLASHEQ) || take(c, T_AMPEQ) ||
        take(c, T_PIPEEQ) || take(c, T_CARETEQ) || take(c, T_PERCENTEQ) ||
        take(c, T_LSHEQ) || take(c, T_RSHEQ)) {
        TokenKind opk = c->tokens[c->pos - 1].kind;
        NodeKind bk = N_ADD;
        int bin;
        t = cur(c);
        right = assignment_expr(c);
        if (right < 0) {
            return -1;
        }
        if (opk == T_ASSIGN) {
            return make_assign(c, left, right, t);
        }
        if (opk == T_MINUSEQ) {
            bk = N_SUB;
        } else if (opk == T_STAREQ) {
            bk = N_MUL;
        } else if (opk == T_SLASHEQ) {
            bk = N_DIV;
        } else if (opk == T_AMPEQ) {
            bk = N_BITAND;
        } else if (opk == T_PIPEEQ) {
            bk = N_BITOR;
        } else if (opk == T_CARETEQ) {
            bk = N_BITXOR;
        } else if (opk == T_PERCENTEQ) {
            bk = N_MOD;
        } else if (opk == T_LSHEQ) {
            bk = N_SHL;
        } else if (opk == T_RSHEQ) {
            bk = N_SHR;
        }
        bin = node(c, bk, t);
        if (bin < 0) {
            return -1;
        }
        c->nodes[bin].left = left;
        c->nodes[bin].right = right;
        /* Propagate float-ness like the normal binary() path; otherwise
         * compound assignment such as `*p += 2.5` or `f += 2.5` generated
         * integer ops. value_is_float keeps pointer arithmetic integer. */
        c->nodes[bin].is_float =
            (uint8_t)(value_is_float(c, left) || value_is_float(c, right));
        return make_assign(c, left, bin, t);
    }
    return left;
}

static int expression(Compiler *c) {
    int left = assignment_expr(c);
    while (left >= 0 && take(c, T_COMMA)) {
        Token *t = cur(c);
        int right = assignment_expr(c);
        int id;
        if (right < 0) {
            return -1;
        }
        id = node(c, N_COMMA, t);
        if (id < 0) {
            return -1;
        }
        c->nodes[id].left = left;
        c->nodes[id].right = right;
        left = id;
    }
    return left;
}

static uint8_t ginit_store_w(uint8_t w) {
    if (w >= 8) {
        return CL_OP_STORE64;
    }
    if (w == 1) {
        return CL_OP_STOREB;
    }
    return CL_OP_STORE;
}

static int ginit_add(Compiler *c, uint32_t addr, uint8_t w, int expr) {
    Token *t = cur(c);
    if (expr < 0) {
        return 0;
    }
    if (c->nginits >= GINIT_MAX) {
        return fail(c, t->line, t->column, "too many global initializers");
    }
    c->ginit_addr[c->nginits] = addr;
    c->ginit_w[c->nginits] = w ? w : 4u;
    c->ginit_expr[c->nginits] = expr;
    c->nginits++;
    return 1;
}

static int skip_one_init(Compiler *c) {
    int brace = 0;
    int paren = 0;
    int brack = 0;
    if (cur(c)->kind == T_LB) {
        brace = 1;
        ++c->pos;
        while (c->pos < c->ntok && brace > 0) {
            if (cur(c)->kind == T_LB) {
                brace++;
            } else if (cur(c)->kind == T_RB) {
                brace--;
            }
            ++c->pos;
        }
        return 1;
    }
    while (c->pos < c->ntok) {
        TokenKind k = cur(c)->kind;
        if (paren == 0 && brack == 0 && brace == 0 &&
            (k == T_COMMA || k == T_RB || k == T_SEMI)) {
            return 1;
        }
        if (k == T_LP) {
            paren++;
        } else if (k == T_RP && paren > 0) {
            paren--;
        } else if (k == T_LBRACK) {
            brack++;
        } else if (k == T_RBRACK && brack > 0) {
            brack--;
        } else if (k == T_LB) {
            brace++;
        } else if (k == T_RB && brace > 0) {
            brace--;
        }
        ++c->pos;
    }
    return 1;
}

static int count_brace_inits(Compiler *c) {
    int saved = c->pos;
    int n = 0;
    if (cur(c)->kind != T_LB) {
        return 1;
    }
    ++c->pos;
    while (cur(c)->kind != T_RB && cur(c)->kind != T_EOF) {
        if (cur(c)->kind == T_COMMA) {
            ++c->pos;
            continue;
        }
        if (!skip_one_init(c)) {
            break;
        }
        n++;
        take(c, T_COMMA);
    }
    c->pos = saved;
    return n;
}

static uint8_t field_init_w(const StructDef *s, int fi) {
    if (s->fptr[fi] || s->fwidth[fi] >= 8) {
        return 8;
    }
    if (s->packed && s->fwidth[fi] == 1) {
        return 1;
    }
    return 4;
}

static int parse_ginit_struct(Compiler *c, uint32_t addr, int sid);
static int parse_ginit_array(Compiler *c, uint32_t addr, int count, int sid,
                             uint16_t stride, uint8_t elem_w);

static int parse_ginit_scalar(Compiler *c, uint32_t addr, uint8_t w) {
    int gi;
    if (take(c, T_LB)) {
        gi = assignment_expr(c);
        if (gi < 0) {
            return 0;
        }
        take(c, T_COMMA);
        while (cur(c)->kind != T_RB && cur(c)->kind != T_EOF) {
            skip_one_init(c);
            take(c, T_COMMA);
        }
        if (!expect(c, T_RB, "expected } after initializer")) {
            return 0;
        }
        return ginit_add(c, addr, w, gi);
    }
    gi = assignment_expr(c);
    if (gi < 0) {
        return 0;
    }
    return ginit_add(c, addr, w, gi);
}

static int parse_ginit_struct(Compiler *c, uint32_t addr, int sid) {
    StructDef *s;
    int braced;
    int fi;
    if (sid < 0 || sid >= c->nstructs) {
        return parse_ginit_scalar(c, addr, 4);
    }
    s = &c->structs[sid];
    braced = take(c, T_LB);
    fi = 0;
    while (fi < s->nfields) {
        uint32_t fa;
        int nested;
        if (braced && (cur(c)->kind == T_RB || cur(c)->kind == T_EOF)) {
            break;
        }
        if (braced && cur(c)->kind == T_COMMA) {
            ++c->pos;
            if (cur(c)->kind == T_RB) {
                break;
            }
            continue;
        }
        fa = addr + (uint32_t)s->foff[fi];
        nested = s->fstruct[fi];
        if (!s->fptr[fi] && nested >= 0 && s->felemw[fi] == 0) {
            if (!parse_ginit_struct(c, fa, nested)) {
                return 0;
            }
        } else if (!s->fptr[fi] && s->felemw[fi] > 0) {
            int n = s->felemw[fi] ? (int)s->fwidth[fi] / (int)s->felemw[fi] : 1;
            uint16_t st = s->felemw[fi];
            if (n < 1) {
                n = 1;
            }
            if (!parse_ginit_array(c, fa, n, nested, st, (uint8_t)st)) {
                return 0;
            }
        } else {
            if (!parse_ginit_scalar(c, fa, field_init_w(s, fi))) {
                return 0;
            }
        }
        fi++;
        if (braced) {
            take(c, T_COMMA);
            if (cur(c)->kind == T_RB) {
                break;
            }
        } else if (fi < s->nfields) {
            if (!take(c, T_COMMA)) {
                break;
            }
        }
    }
    if (braced) {
        while (cur(c)->kind != T_RB && cur(c)->kind != T_EOF) {
            skip_one_init(c);
            take(c, T_COMMA);
        }
        if (!expect(c, T_RB, "expected } after struct initializer")) {
            return 0;
        }
    }
    return 1;
}

static int parse_ginit_array(Compiler *c, uint32_t addr, int count, int sid,
                             uint16_t stride, uint8_t elem_w) {
    int i = 0;
    uint16_t step = stride ? stride : (elem_w ? elem_w : 4u);
    if (!take(c, T_LB)) {
        if (sid >= 0) {
            return parse_ginit_struct(c, addr, sid);
        }
        return parse_ginit_scalar(c, addr, elem_w ? elem_w : 4u);
    }
    while (cur(c)->kind != T_RB && cur(c)->kind != T_EOF && i < count) {
        uint32_t ea;
        if (cur(c)->kind == T_COMMA) {
            ++c->pos;
            continue;
        }
        ea = addr + (uint32_t)i * (uint32_t)step;
        if (sid >= 0) {
            if (!parse_ginit_struct(c, ea, sid)) {
                return 0;
            }
        } else {
            if (!parse_ginit_scalar(c, ea, elem_w ? elem_w : 4u)) {
                return 0;
            }
        }
        i++;
        take(c, T_COMMA);
    }
    while (cur(c)->kind != T_RB && cur(c)->kind != T_EOF) {
        skip_one_init(c);
        take(c, T_COMMA);
    }
    if (!expect(c, T_RB, "expected } after array initializer")) {
        return 0;
    }
    return 1;
}

static int parse_ginit_for_sym(Compiler *c, int gsym) {
    Symbol *s = &c->syms[gsym];
    uint32_t addr = s->address;
    int sid = s->struct_id;
    uint8_t w = 4;
    if (s->is_ptr || s->width >= 8) {
        w = 8;
    } else if (s->packed || s->width == 1) {
        w = 1;
    }
    if (cur(c)->kind != T_LB) {
        int gi = assignment_expr(c);
        if (gi < 0) {
            return 0;
        }
        return ginit_add(c, addr, w, gi);
    }
    if (s->is_array) {
        int n = s->array_n ? (int)s->array_n : 1;
        uint16_t st = s->stride ? s->stride : (s->width ? s->width : 4u);
        uint8_t ew = (uint8_t)(st > 255u ? 255u : st);
        if (sid < 0) {
            ew = w;
            st = ew;
        }
        return parse_ginit_array(c, addr, n, sid, st, ew);
    }
    if (sid >= 0 && !s->is_ptr) {
        return parse_ginit_struct(c, addr, sid);
    }
    return parse_ginit_scalar(c, addr, w);
}

static int finish_decl(Compiler *c, int decl_id, Token *t) {
    int sym;
    int v;
    if (decl_id < 0) {
        return -1;
    }
    sym = c->nodes[decl_id].value;
    if (take(c, T_ASSIGN)) {
        if (take(c, T_LB)) {
            int blk = node(c, N_BLOCK, t);
            int last = decl_id;
            int seq = 0;
            if (blk < 0) {
                return -1;
            }
            c->nodes[decl_id].left = -1;
            while (!take(c, T_RB) && cur(c)->kind != T_EOF) {
                int id = -1;
                int fi = -1;
                int idxv = seq;
                int desig = 0;
                if (cur(c)->kind == T_LB && c->syms[sym].struct_id >= 0) {
                    int sid = c->syms[sym].struct_id;
                    int fi = 0;
                    int idxv = seq;
                    take(c, T_LB);
                    while (!take(c, T_RB) && cur(c)->kind != T_EOF) {
                        int v2;
                        int id2;
                        if (cur(c)->kind == T_COMMA) {
                            ++c->pos;
                            continue;
                        }
                        if (cur(c)->kind == T_LB) {
                            skip_one_init(c);
                            take(c, T_COMMA);
                            fi++;
                            continue;
                        }
                        v2 = assignment_expr(c);
                        if (v2 < 0) {
                            return -1;
                        }
                        take(c, T_COMMA);
                        id2 = node(c, N_FIELD_ASSIGN, t);
                        if (id2 < 0) {
                            return -1;
                        }
                        c->nodes[id2].value = sym;
                        c->nodes[id2].left = v2;
                        c->nodes[id2].third = fi;
                        if (c->syms[sym].is_array) {
                            int ix = node(c, N_INT, t);
                            if (ix < 0) {
                                return -1;
                            }
                            c->nodes[ix].value = idxv;
                            c->nodes[id2].right = ix;
                        } else {
                            c->nodes[id2].right = -1;
                        }
                        (void)sid;
                        c->nodes[last].next = id2;
                        last = id2;
                        fi++;
                    }
                    take(c, T_COMMA);
                    seq++;
                    continue;
                }
                if (cur(c)->kind == T_LB) {
                    skip_one_init(c);
                    take(c, T_COMMA);
                    seq++;
                    continue;
                }
                if (take(c, T_LBRACK)) {
                    Token *num = cur(c);
                    if (num->kind != T_NUM) {
                        return fail(c, num->line, num->column,
                                    "expected index in designator");
                    }
                    idxv = (int)num->value;
                    ++c->pos;
                    if (!expect(c, T_RBRACK, "expected ]") ||
                        !expect(c, T_ASSIGN, "expected =")) {
                        return -1;
                    }
                    desig = 1;
                } else if (take(c, T_DOT)) {
                    Token *fld = cur(c);
                    if (!expect(c, T_ID, "expected field") ||
                        !expect(c, T_ASSIGN, "expected =")) {
                        return -1;
                    }
                    if (c->syms[sym].struct_id < 0) {
                        return fail(c, fld->line, fld->column, "not a struct");
                    }
                    fi = field_find(&c->structs[c->syms[sym].struct_id],
                                    fld->name);
                    if (fi < 0) {
                        return fail(c, fld->line, fld->column, "unknown field");
                    }
                    desig = 1;
                }
                v = assignment_expr(c);
                if (v < 0) {
                    return -1;
                }
                take(c, T_COMMA);
                if (fi >= 0) {
                    id = node(c, N_FIELD_ASSIGN, t);
                    if (id < 0) {
                        return -1;
                    }
                    c->nodes[id].value = sym;
                    c->nodes[id].left = v;
                    c->nodes[id].right = -1;
                    c->nodes[id].third = fi;
                } else if (c->syms[sym].is_array) {
                    int ix = node(c, N_INT, t);
                    if (ix < 0) {
                        return -1;
                    }
                    c->nodes[ix].value = idxv;
                    id = node(c, N_INDEX_ASSIGN, t);
                    if (id < 0) {
                        return -1;
                    }
                    c->nodes[id].value = sym;
                    c->nodes[id].left = v;
                    c->nodes[id].right = ix;
                } else {
                    id = node(c, N_ASSIGN, t);
                    if (id < 0) {
                        return -1;
                    }
                    c->nodes[id].value = sym;
                    c->nodes[id].left = v;
                }
                c->nodes[last].next = id;
                last = id;
                if (!desig) {
                    seq++;
                } else {
                    seq = idxv + 1;
                }
            }
            if (!expect(c, T_SEMI, "expected ; after declaration")) {
                return -1;
            }
            c->nodes[blk].left = decl_id;
            return blk;
        }
        v = assignment_expr(c);
        if (v < 0) {
            return -1;
        }
        c->nodes[decl_id].left = v;
    }
    {
        int head = decl_id;
        int last = decl_id;
        while (take(c, T_COMMA)) {
            Token *n2;
            int s2;
            int d2;
            uint8_t ptr = 0;
            Symbol *base = &c->syms[sym];
            while (take(c, T_STAR)) {
                ptr = 1;
            }
            n2 = cur(c);
            if (!expect(c, T_ID, "expected identifier")) {
                return -1;
            }
            s2 = sym_add(c, n2, base->is_float, ptr ? 8 : base->width, ptr);
            if (s2 < 0) {
                return -1;
            }
            d2 = node(c, N_DECL, n2);
            if (d2 < 0) {
                return -1;
            }
            c->nodes[d2].value = s2;
            if (take(c, T_ASSIGN)) {
                v = assignment_expr(c);
                if (v < 0) {
                    return -1;
                }
                c->nodes[d2].left = v;
            }
            c->nodes[last].next = d2;
            last = d2;
        }
        if (!expect(c, T_SEMI, "expected ; after declaration")) {
            return -1;
        }
        if (c->nodes[head].next >= 0) {
            int blk = node(c, N_BLOCK, t);
            if (blk < 0) {
                return -1;
            }
            c->nodes[blk].left = head;
            return blk;
        }
        return head;
    }
}

static int block(Compiler *c);
static int statement(Compiler *c);

static int stmt_or_block(Compiler *c) {
    Token *t = cur(c);
    int s;
    int id;
    if (t->kind == T_LB) {
        return block(c);
    }
    s = statement(c);
    if (s < 0) {
        return -1;
    }
    id = node(c, N_BLOCK, t);
    if (id < 0) {
        return -1;
    }
    c->nodes[id].left = s;
    return id;
}

static int for_step(Compiler *c) {
    Token *t = cur(c);
    int id;
    int v;

    if (cur(c)->kind == T_RP) {
        id = node(c, N_BLOCK, t);
        if (id >= 0) {
            c->nodes[id].left = -1;
        }
        return id;
    }
    v = expression(c);
    if (v < 0) {
        return -1;
    }
    id = node(c, N_EXPR, t);
    if (id < 0) {
        return -1;
    }
    c->nodes[id].left = v;
    return id;
}

static int for_init(Compiler *c) {
    Token *t = cur(c);
    Token *name;
    int id;
    int sym;
    int v;

    if (cur(c)->kind == T_SEMI) {
        id = node(c, N_BLOCK, t);
        if (id >= 0) {
            c->nodes[id].left = -1;
        }
        return id;
    }
    {
        DeclType dt;
        dt.w = 4;
        dt.ptr = 0;
        dt.isf = 0;
        dt.uns = 0;
        dt.is_void = 0;
        dt.sid = -1;
        dt.arr_n = 0;
        dt.elemw = 0;
        if (parse_type_n(c, &dt, 0, 0)) {
            int dw = dt.ptr ? 8 : (dt.w > 255 ? 255 : (int)dt.w);
            name = cur(c);
            if (!expect(c, T_ID, "expected variable name")) {
                return -1;
            }
            sym = sym_add(c, name, dt.isf, (uint8_t)(dw ? dw : 4), dt.ptr);
            if (sym < 0) {
                return -1;
            }
            c->syms[sym].is_unsigned = dt.uns;
            if (dt.sid >= 0) {
                c->syms[sym].struct_id = dt.sid;
                if (dt.ptr)
                    c->syms[sym].pointee = c->structs[dt.sid].size
                                               ? c->structs[dt.sid].size
                                               : 4;
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
    }
    if (t->kind == T_ID || t->kind == T_PLUSPLUS || t->kind == T_MINUSMINUS) {
        v = expression(c);
        if (v < 0) {
            return -1;
        }
        id = node(c, N_EXPR, t);
        if (id < 0) {
            return -1;
        }
        c->nodes[id].left = v;
        return id;
    }
    return fail(c, t->line, t->column, "expected for init");
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
    uint8_t uns;

    maybe_yield();
    uns = take_qualifiers(c);
    if (take(c, T_SEMI)) {
        id = node(c, N_BLOCK, t);
        if (id >= 0) {
            c->nodes[id].left = -1;
        }
        return id;
    }
    if (cur(c)->kind == T_LB) {
        return block(c);
    }
    if (take(c, T_ALIGNAS)) {
        if (!expect(c, T_LP, "expected ( after _Alignas")) {
            return -1;
        }
        {
            int al = 4;
            if (!parse_const_int(c, &al)) {
                return -1;
            }
            if (!expect(c, T_RP, "expected ) after _Alignas")) {
                return -1;
            }
            if (al < 1)
                al = 1;
            c->align_next = al;
        }
    }
    if (take(c, T_ENUM)) {
        take(c, T_ID);
        if (take(c, T_LB)) {
            if (!parse_enum_list(c)) {
                return -1;
            }
        }
        if (cur(c)->kind == T_ID) {
            name = cur(c);
            ++c->pos;
            if (take(c, T_LBRACK)) {
                int count = 1;
                if (!parse_array_dim(c, &count)) {
                    return -1;
                }
                sym = 0;
                (void)count;
                (void)name;
            }
            /* enum-typed locals are ints */
            if (sym_find_local(c, name->name) < 0) {
                sym = sym_add(c, name, 0, 4, 0);
                if (sym < 0) {
                    return -1;
                }
                id = node(c, N_DECL, t);
                if (id < 0) {
                    return -1;
                }
                c->nodes[id].value = sym;
                if (!expect(c, T_SEMI, "expected ; after enum")) {
                    return -1;
                }
                return id;
            }
        }
        if (!expect(c, T_SEMI, "expected ; after enum")) {
            return -1;
        }
        id = node(c, N_BLOCK, t);
        if (id >= 0) {
            c->nodes[id].left = -1;
            c->nodes[id].value = 0;
        }
        return id;
    }
    {
        uint8_t w = 0;
        uint8_t ptr = 0;
        if (take(c, T_LONG)) {
            w = 8;
            take(c, T_INT);
        } else if (take(c, T_SHORT)) {
            w = 2;
            take(c, T_INT);
        } else if (take(c, T_BOOL))
            w = 1;
        else if (take(c, T_INT))
            w = 4;
        else if (take(c, T_VOID))
            w = 4;
        if (!w && uns && cur(c)->kind == T_ID &&
            typedef_find(c, cur(c)->name) < 0) {
            w = 4;
        }
        if (w) {
        if (take(c, T_LP)) {
            int count = 1;
            while (take(c, T_STAR) || take(c, T_CONST) || take(c, T_VOLATILE)) {
                ptr = 1;
            }
            name = cur(c);
            if (!expect(c, T_ID, "expected variable name")) {
                return -1;
            }
            if (take(c, T_LBRACK)) {
                if (!parse_array_dim(c, &count)) {
                    return -1;
                }
            }
            if (!expect(c, T_RP, "expected ) in function pointer type")) {
                return -1;
            }
            if (cur(c)->kind == T_LP && !skip_paren_depth(c)) {
                return fail(c, cur(c)->line, cur(c)->column, "bad function pointer type");
            }
            ptr = 1;
            if (count > 1) {
                sym = sym_add_array(c, name, count, 0, 0, 8);
            } else {
                sym = sym_add(c, name, 0, 8, 1);
            }
            if (sym < 0) {
                return -1;
            }
            c->syms[sym].is_unsigned = uns;
            id = node(c, N_DECL, t);
            if (id < 0) {
                return -1;
            }
            c->nodes[id].value = sym;
            return finish_decl(c, id, t);
        }
        while (take(c, T_STAR)) {
            ptr = 1;
        }
        name = cur(c);
        if (!expect(c, T_ID, "expected variable name")) {
            return -1;
        }
        if (cur(c)->kind == T_LP) {
            if (!skip_paren_depth(c)) {
                return fail(c, name->line, name->column, "expected (");
            }
            if (!expect(c, T_SEMI, "expected ; after prototype")) {
                return -1;
            }
            id = node(c, N_BLOCK, t);
            if (id >= 0) {
                c->nodes[id].left = -1;
            }
            return id;
        }
        if (take(c, T_LBRACK)) {
            int count = 1;
            if (!parse_array_dim(c, &count)) {
                return -1;
            }
            sym = sym_add_array(c, name, count, 0, 0, ptr ? 8 : w);
        } else {
            if (take(c, T_COLON)) {
                return fail(c, name->line, name->column,
                            "bitfield only inside a struct");
            }
            sym = sym_add(c, name, 0, w, ptr);
        }
        if (sym < 0) {
            return -1;
        }
        c->syms[sym].is_unsigned = uns;
        id = node(c, N_DECL, t);
        if (id < 0) {
            return -1;
        }
        c->nodes[id].value = sym;
        return finish_decl(c, id, t);
        }
    }
    if (take(c, T_FLOAT)) {
        uint8_t ptr = 0;
        int complex = take(c, T_COMPLEX);
        while (take(c, T_STAR)) {
            ptr = 1;
        }
        name = cur(c);
        if (!expect(c, T_ID, "expected variable name")) {
            return -1;
        }
        if (take(c, T_LBRACK)) {
            int count = 1;
            if (!parse_array_dim(c, &count)) {
                return -1;
            }
            sym = sym_add_array(c, name, count, 1, 0, 4);
        } else if (complex && !ptr) {
            int cs = ensure_complex(c);
            sym = sym_add(c, name, 0, 8, 0);
            if (sym >= 0 && cs >= 0) {
                c->syms[sym].struct_id = (int16_t)cs;
                c->syms[sym].is_complex = 1;
            }
        } else {
            sym = sym_add(c, name, 1, 4, ptr);
        }
        if (sym < 0) {
            return -1;
        }
        c->syms[sym].is_unsigned = uns;
        id = node(c, N_DECL, t);
        if (id < 0) {
            return -1;
        }
        c->nodes[id].value = sym;
        return finish_decl(c, id, t);
    }
    if (take(c, T_CHAR)) {
        uint8_t ptr = 0;
        while (take(c, T_STAR)) {
            ptr = 1;
        }
        name = cur(c);
        if (!expect(c, T_ID, "expected variable name")) {
            return -1;
        }
        if (take(c, T_LBRACK)) {
            int count = 1;
            if (!parse_array_dim(c, &count)) {
                return -1;
            }
            sym = sym_add_array(c, name, count, 0, ptr ? 0 : 1, ptr ? 8 : 1);
        } else {
            sym = sym_add(c, name, 0, 1, ptr);
        }
        if (sym < 0) {
            return -1;
        }
        c->syms[sym].is_unsigned = uns;
        id = node(c, N_DECL, t);
        if (id < 0) {
            return -1;
        }
        c->nodes[id].value = sym;
        return finish_decl(c, id, t);
    }
    {
        int ti;
        for (ti = 0; ti < c->ntypedef; ++ti) {
            if (cur(c)->kind == T_ID && same(c->td_name[ti], cur(c)->name) &&
                (c->tokens[c->pos + 1].kind == T_ID ||
                 c->tokens[c->pos + 1].kind == T_STAR ||
                 c->tokens[c->pos + 1].kind == T_LBRACK)) {
                uint16_t live = typedef_live_width(c, ti);
                uint8_t w = (uint8_t)(live > 255 ? 255 : live);
                uint8_t ptr = (uint8_t)c->td_ptr[ti];
                int nstar = 0;
                DeclType td;
                ++c->pos;
                td.w = live;
                td.ptr = ptr;
                td.isf = 0;
                td.uns = uns;
                td.is_void = 0;
                td.sid = (int16_t)c->td_struct[ti];
                td.arr_n = (uint16_t)c->td_count[ti];
                td.elemw = (uint16_t)c->td_elemw[ti];
                while (take(c, T_STAR)) {
                    ptr = 1;
                    nstar++;
                    td.ptr = 1;
                    td.arr_n = 0;
                    td.elemw = 0;
                }
                if (nstar > 1) {
                    w = 8;
                    td.w = 8;
                }
                name = cur(c);
                if (!expect(c, T_ID, "expected variable name")) {
                    return -1;
                }
                if (take(c, T_LBRACK)) {
                    int count = 1;
                    uint8_t ew;
                    if (!parse_array_dim(c, &count)) {
                        return -1;
                    }
                    if (ptr)
                        ew = 8;
                    else if (td.arr_n > 1)
                        ew = (uint8_t)(td.w > 255 ? 255 : td.w);
                    else
                        ew = w ? w : 4u;
                    sym = sym_add_array(c, name, count, 0, (ew <= 1 && !ptr) ? 1 : 0,
                                        ew);
                } else if (td.arr_n > 1 && !ptr) {
                    sym = sym_add_typedef_array(c, name, &td);
                } else {
                    sym = sym_add(c, name, 0, w, ptr);
                }
                if (sym < 0) {
                    return -1;
                }
                c->syms[sym].is_unsigned = uns;
                c->syms[sym].struct_id = (int16_t)c->td_struct[ti];
                if (ptr && nstar <= 1 && c->td_struct[ti] >= 0) {
                    uint16_t sz = c->structs[c->td_struct[ti]].size;
                    if (sz) {
                        c->syms[sym].pointee = sz;
                        c->syms[sym].stride = sz;
                    }
                }
                id = node(c, N_DECL, t);
                if (id < 0) {
                    return -1;
                }
                c->nodes[id].value = sym;
                return finish_decl(c, id, t);
            }
        }
    }
    {
        int is_u = 0;
        if (take(c, T_UNION)) {
            is_u = 1;
        } else if (!take(c, T_STRUCT)) {
            is_u = -1;
        }
        if (is_u >= 0) {
        Token *tn;
        Token *vn;
        int sid;
        int count = 1;
        StructDef *st;
        uint32_t need;
        if (cur(c)->kind == T_LB) {
            if (!parse_struct_def(c, is_u)) {
                return -1;
            }
            sid = c->nstructs - 1;
        } else {
            tn = cur(c);
            if (!expect(c, T_ID, "expected struct type")) {
                return -1;
            }
            sid = struct_find(c, tn->name);
            if (sid < 0) {
                fail(c, tn->line, tn->column, "unknown struct");
                return -1;
            }
        }
        vn = cur(c);
        {
            uint8_t sptr = 0;
            while (take(c, T_STAR)) {
                sptr = 1;
            }
            vn = cur(c);
            if (!expect(c, T_ID, "expected variable name")) {
                return -1;
            }
        st = &c->structs[sid];
        if (sptr) {
            if (take(c, T_LBRACK)) {
                int nptr = 1;
                if (!parse_array_dim(c, &nptr)) {
                    return -1;
                }
                sym = sym_add_array(c, vn, nptr, 0, 0, 8);
                if (sym < 0) {
                    return -1;
                }
                c->syms[sym].is_ptr = 1;
                c->syms[sym].is_unsigned = uns;
                c->syms[sym].struct_id = (int16_t)sid;
                c->syms[sym].pointee = st->size ? st->size : 4;
                c->syms[sym].stride = 8;
                id = node(c, N_DECL, t);
                if (id < 0) {
                    return -1;
                }
                c->nodes[id].value = sym;
                c->nodes[id].left = -1;
                return finish_decl(c, id, t);
            }
            sym = sym_add(c, vn, 0, 8, 1);
            if (sym < 0) {
                return -1;
            }
            c->syms[sym].is_unsigned = uns;
            c->syms[sym].struct_id = (int16_t)sid;
            c->syms[sym].pointee = st->size;
            c->syms[sym].stride = st->size ? st->size : 8;
            id = node(c, N_DECL, t);
            if (id < 0) {
                return -1;
            }
            c->nodes[id].value = sym;
            c->nodes[id].left = -1;
            return finish_decl(c, id, t);
        }
        if (take(c, T_LBRACK)) {
            if (!parse_array_dim(c, &count)) {
                return -1;
            }
            if (count < 1 || count > 65536) {
                fail(c, vn->line, vn->column, "array size out of range");
                return -1;
            }
        }
        if (sym_find_local(c, vn->name) >= 0) {
            fail(c, vn->line, vn->column, "duplicate variable");
            return -1;
        }
        if (c->nsyms == SYM_MAX) {
            fail(c, vn->line, vn->column, "symbol table full");
            return -1;
        }
        need = (uint32_t)count * (uint32_t)st->size;
        if ((uint32_t)c->mem_next + need > CLVM_MEMORY_SIZE) {
            fail(c, vn->line, vn->column, "struct exceeds memory");
            return -1;
        }
        if (c->align_next > 1) {
            uint32_t a = (uint32_t)c->align_next;
            if ((a & (a - 1u)) == 0)
                c->mem_next = (c->mem_next + a - 1u) & ~(a - 1u);
            c->align_next = 0;
        }
        sym = c->nsyms++;
        decl_name(c, vn, c->syms[sym].name);
        c->syms[sym].address = c->mem_next;
        c->syms[sym].is_float = 0;
        c->syms[sym].packed = 0;
        c->syms[sym].is_array = (uint8_t)(count > 1);
        c->syms[sym].is_ptr = 0;
        c->syms[sym].is_unsigned = 0;
        c->syms[sym].is_const = c->saw_const;
        c->syms[sym].is_complex = 0;
        c->saw_const = 0;
        c->syms[sym].is_global = 0;
        c->syms[sym].scope = (uint16_t)scope_cur(c);
        c->syms[sym].width = 4;
        c->syms[sym].pointee = 0;
        c->syms[sym].struct_id = (int16_t)sid;
        c->syms[sym].stride = st->size;
        c->syms[sym].array_n = (uint16_t)(count > 65535 ? 65535 : count);
        c->mem_next += need;
        id = node(c, N_DECL, t);
        if (id < 0) {
            return -1;
        }
        c->nodes[id].value = sym;
        c->nodes[id].left = -1;
        return finish_decl(c, id, t);
        }
        }
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
        if (cur(c)->kind == T_SEMI) {
            v = node(c, N_INT, t);
            if (v >= 0) {
                c->nodes[v].value = 1;
            }
        } else {
            v = expression(c);
        }
        if (v < 0 || !expect(c, T_SEMI, "expected ; after for condition")) {
            return -1;
        }
        step = for_step(c);
        if (step < 0 || !expect(c, T_RP, "expected ) after for")) {
            return -1;
        }
        b = stmt_or_block(c);
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
        b = stmt_or_block(c);
        if (b < 0) {
            return -1;
        }
        c->nodes[id].left = v;
        c->nodes[id].right = b;
        if (take(c, T_ELSE)) {
            b = stmt_or_block(c);
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
        b = stmt_or_block(c);
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
    if (take(c, T_BREAK)) {
        id = node(c, N_BREAK, t);
        if (id < 0 || !expect(c, T_SEMI, "expected ; after break")) {
            return -1;
        }
        return id;
    }
    if (take(c, T_CONTINUE)) {
        id = node(c, N_CONTINUE, t);
        if (id < 0 || !expect(c, T_SEMI, "expected ; after continue")) {
            return -1;
        }
        return id;
    }
    if (take(c, T_DO)) {
        id = node(c, N_DO, t);
        if (id < 0) {
            return -1;
        }
        b = block(c);
        if (b < 0 || !expect(c, T_WHILE, "expected while after do") ||
            !expect(c, T_LP, "expected ( after while")) {
            return -1;
        }
        v = expression(c);
        if (v < 0 || !expect(c, T_RP, "expected )") ||
            !expect(c, T_SEMI, "expected ; after do-while")) {
            return -1;
        }
        c->nodes[id].left = v;
        c->nodes[id].right = b;
        return id;
    }
    if (take(c, T_SWITCH)) {
        id = node(c, N_SWITCH, t);
        if (id < 0 || !expect(c, T_LP, "expected ( after switch")) {
            return -1;
        }
        v = expression(c);
        if (v < 0 || !expect(c, T_RP, "expected ) after switch")) {
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
    if (take(c, T_CASE)) {
        int cv = 0;
        if (!parse_const_int(c, &cv) ||
            !expect(c, T_COLON, "expected : after case")) {
            return -1;
        }
        id = node(c, N_CASE, t);
        if (id >= 0) {
            c->nodes[id].value = cv;
        }
        return id;
    }
    if (take(c, T_DEFAULT)) {
        if (!expect(c, T_COLON, "expected : after default")) {
            return -1;
        }
        return node(c, N_DEFAULT, t);
    }
    if (take(c, T_GOTO)) {
        Token *lab = cur(c);
        if (!expect(c, T_ID, "expected label") ||
            !expect(c, T_SEMI, "expected ; after goto")) {
            return -1;
        }
        id = node(c, N_GOTO, t);
        if (id >= 0) {
            text(c->nodes[id].name, NAME_MAX, lab->name);
        }
        return id;
    }
    if (take(c, T_ASM)) {
        char buf[1024];
        int bi = 0;
        int depth = 0;
        if (!expect(c, T_LB, "expected { after asm")) {
            return -1;
        }
        depth = 1;
        while (cur(c)->kind != T_EOF && depth > 0) {
            Token *tk = cur(c);
            if (tk->kind == T_LB) {
                ++depth;
            }
            if (tk->kind == T_RB) {
                --depth;
                if (depth == 0) {
                    break;
                }
            }
            if (tk->kind == T_ID || tk->kind == T_NUM) {
                int k = 0;
                const char *nm = tk->kind == T_ID ? tk->name : 0;
                char tmp[32];
                if (tk->kind == T_NUM) {
                    int val = tk->value;
                    int n = 0;
                    char rev[16];
                    if (val < 0) {
                        if (bi + 1 < 1024) buf[bi++] = '-';
                        val = -val;
                    }
                    if (val == 0) {
                        rev[n++] = '0';
                    }
                    while (val && n < 15) {
                        rev[n++] = (char)('0' + (val % 10));
                        val /= 10;
                    }
                    while (n--) {
                        if (bi + 1 < 1024) buf[bi++] = rev[n];
                    }
                    (void)tmp;
                } else {
                    while (nm[k] && bi + 1 < 1024) {
                        buf[bi++] = nm[k++];
                    }
                }
                if (bi + 1 < 1024) buf[bi++] = ' ';
            }
            ++c->pos;
        }
        if (!expect(c, T_RB, "expected } after asm")) {
            return -1;
        }
        buf[bi] = 0;
        id = node(c, N_ASM, t);
        if (id >= 0) {
            text(c->nodes[id].name, NAME_MAX, buf);
            c->nodes[id].value = c->str_len;
            {
                int k = 0;
                while (buf[k] && c->str_len + 1 < STR_POOL_MAX) {
                    c->str_pool[c->str_len++] = buf[k++];
                }
                if (c->str_len + 1 < STR_POOL_MAX) {
                    c->str_pool[c->str_len++] = 0;
                }
            }
        }
        return id;
    }
    if (cur(c)->kind == T_ID && c->tokens[c->pos + 1].kind == T_COLON) {
        name = cur(c);
        c->pos += 2;
        id = node(c, N_LABEL, name);
        if (id >= 0) {
            text(c->nodes[id].name, NAME_MAX, name->name);
        }
        return id;
    }
    if (take(c, T_STATIC_ASSERT)) {
        int ev;
        if (!expect(c, T_LP, "expected ( after _Static_assert")) {
            return -1;
        }
        ev = expression(c);
        if (ev < 0) {
            return -1;
        }
        take(c, T_COMMA);
        if (cur(c)->kind == T_STR) {
            ++c->pos;
        }
        if (!expect(c, T_RP, "expected )") ||
            !expect(c, T_SEMI, "expected ; after static_assert")) {
            return -1;
        }
        if (c->nodes[ev].kind == N_INT && c->nodes[ev].value == 0) {
            return fail(c, t->line, t->column, "static_assert failed");
        }
        id = node(c, N_EXPR, t);
        c->nodes[id].left = ev;
        return id;
    }
    if (cur(c)->kind == T_ID &&
        (c->tokens[c->pos + 1].kind == T_ASSIGN ||
         c->tokens[c->pos + 1].kind == T_PLUSPLUS ||
         c->tokens[c->pos + 1].kind == T_MINUSMINUS ||
         c->tokens[c->pos + 1].kind == T_PLUSEQ ||
         c->tokens[c->pos + 1].kind == T_MINUSEQ ||
         c->tokens[c->pos + 1].kind == T_STAREQ ||
         c->tokens[c->pos + 1].kind == T_SLASHEQ)) {
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
        } else if (take(c, T_ARROW)) {
            Token *fld = cur(c);
            int fi;
            if (c->syms[sym].struct_id < 0) {
                fail(c, name->line, name->column, "not a struct pointer");
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
            c->nodes[id].right = -3;
            c->nodes[id].third = fi;
        } else if (take(c, T_PLUSPLUS) || take(c, T_MINUSMINUS)) {
            int dec = c->tokens[c->pos - 1].kind == T_MINUSMINUS;
            int var = node(c, N_VAR, name);
            if (var < 0) {
                return -1;
            }
            c->nodes[var].value = sym;
            id = node(c, dec ? N_PREDEC : N_PREINC, name);
            if (id < 0) {
                return -1;
            }
            c->nodes[id].left = var;
            c->nodes[id].value = sym;
            {
                int e = node(c, N_EXPR, name);
                c->nodes[e].left = id;
                id = e;
            }
        } else if (take(c, T_PLUSEQ) || take(c, T_MINUSEQ) || take(c, T_STAREQ) ||
                   take(c, T_SLASHEQ)) {
            TokenKind opk = c->tokens[c->pos - 1].kind;
            int var = node(c, N_VAR, name);
            int bin;
            NodeKind bk = N_ADD;
            if (var < 0) {
                return -1;
            }
            c->nodes[var].value = sym;
            c->nodes[var].is_float =
                (uint8_t)(c->syms[sym].is_float && !c->syms[sym].is_ptr);
            v = expression(c);
            if (v < 0) {
                return -1;
            }
            if (opk == T_MINUSEQ) {
                bk = N_SUB;
            } else if (opk == T_STAREQ) {
                bk = N_MUL;
            } else if (opk == T_SLASHEQ) {
                bk = N_DIV;
            }
            bin = node(c, bk, name);
            c->nodes[bin].left = var;
            c->nodes[bin].right = v;
            /* Compound assignment must keep float arithmetic when the target
             * or operand is float; otherwise `f += x` emitted integer ops. */
            c->nodes[bin].is_float =
                (uint8_t)(c->nodes[var].is_float || value_is_float(c, v));
            id = node(c, N_ASSIGN, name);
            c->nodes[id].value = sym;
            c->nodes[id].left = bin;
            c->nodes[id].is_float = c->nodes[bin].is_float;
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
    if (!scope_enter(c)) {
        return fail(c, t->line, t->column, "scopes nested too deep");
    }
    id = node(c, N_BLOCK, t);
    if (id < 0) {
        scope_leave(c);
        return -1;
    }
    while (cur(c)->kind != T_RB && cur(c)->kind != T_EOF) {
        s = statement(c);
        if (s < 0) {
            scope_leave(c);
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
        scope_leave(c);
        return -1;
    }
    scope_leave(c);
    c->nodes[id].left = first;
    return id;
}

static int parse_function(Compiler *c, uint8_t ret, Token *name, int16_t ret_sid) {
    FuncDef *f;
    char fname[NAME_MAX];
    if (c->saw_static && c->tu_id > 0) {
        static_mangle(c, name->name, fname);
    } else {
        text(fname, NAME_MAX, name->name);
    }
    if (c->nfuncs == FUNC_MAX) {
        return fail(c, name->line, name->column, "too many functions");
    }
    if (func_find(c, name->name) >= 0) {
        int ex = func_find(c, name->name);
        f = &c->funcs[ex];
        if (f->body >= 0) {
            if (!skip_paren_depth(c)) {
                return fail(c, name->line, name->column, "expected (");
            }
            if (take(c, T_SEMI)) {
                return 1;
            }
            return fail(c, name->line, name->column, "duplicate function");
        }
        c->cur_fn = ex;
        f->argc = 0;
        f->ret = ret;
        f->ret_sid = ret_sid;
    } else {
        f = &c->funcs[c->nfuncs++];
        text(f->name, NAME_MAX, fname);
        ht_ins_grid(c->ht_func, HT_FUNC_N, fname, c->nfuncs - 1,
                    (const char *)c->funcs, (int)sizeof(FuncDef));
        f->argc = 0;
        f->ret = ret;
        f->body = -1;
        f->entry = -1;
        f->is_main = (uint8_t)same(name->name, "main");
        f->is_varargs = 0;
        f->ret_sid = ret_sid;
        c->cur_fn = c->nfuncs - 1;
    }
    c->scope_base = c->nsyms;
    if (!scope_enter(c)) {
        return fail(c, name->line, name->column, "scopes nested too deep");
    }
    if (!expect(c, T_LP, "expected (")) {
        return 0;
    }
    if (!take(c, T_RP)) {
        do {
            DeclType dt;
            Token *an;
            Token tmp;
            char aname[NAME_MAX];
            int sym;
            uint8_t w;
            uint8_t ptr;
            if (take(c, T_ELLIPSIS)) {
                f->is_varargs = 1;
                break;
            }
            aname[0] = 0;
            if (!parse_type_n(c, &dt, aname, NAME_MAX)) {
                return fail(c, cur(c)->line, cur(c)->column, "expected arg type");
            }
            if (dt.is_void && !dt.ptr &&
                (cur(c)->kind == T_RP || cur(c)->kind == T_COMMA)) {
                break;
            }
            if (aname[0]) {
                tmp.kind = T_ID;
                tmp.line = name->line;
                tmp.column = name->column;
                tmp.value = 0;
                text(tmp.name, NAME_MAX, aname);
                an = &tmp;
            } else if (cur(c)->kind == T_COMMA || cur(c)->kind == T_RP) {
                tmp.kind = T_ID;
                tmp.line = name->line;
                tmp.column = name->column;
                tmp.value = 0;
                tmp.name[0] = '_';
                tmp.name[1] = 'a';
                tmp.name[2] = (char)('0' + (f->argc % 10));
                tmp.name[3] = 0;
                an = &tmp;
            } else {
                an = cur(c);
                if (!expect(c, T_ID, "expected arg name")) {
                    return 0;
                }
            }
            if (take(c, T_LBRACK)) {
                int dummy = 1;
                if (!parse_array_dim(c, &dummy)) {
                    return 0;
                }
                ptr = 1;
                dt.ptr = 1;
            }
            if (f->argc == FUNC_ARG_MAX) {
                return fail(c, an->line, an->column, "too many args");
            }
            ptr = dt.ptr;
            /* C: array parameters decay to pointer to the element type. */
            if (dt.arr_n > 1 && !ptr) {
                ptr = 1;
                w = (uint8_t)(dt.elemw ? dt.elemw : 1u);
            } else {
                /* Pass pointee width into sym_add; it sets storage width to 8. */
                w = (uint8_t)(dt.w ? dt.w : (dt.isf ? 4 : 4));
                if (!ptr && w >= 8)
                    w = 8;
            }
            if (ptr && dt.is_void)
                w = 1;
            sym = sym_add(c, an, dt.isf, w, ptr);
            if (sym < 0) {
                return 0;
            }
            c->syms[sym].is_unsigned = dt.uns;
            if (dt.sid >= 0) {
                c->syms[sym].struct_id = dt.sid;
                if (ptr) {
                    uint16_t sz = c->structs[dt.sid].size;
                    c->syms[sym].pointee = sz;
                    c->syms[sym].stride = sz ? sz : 8;
                }
            }
            /* A pointer argument's *value* is a 64-bit address, never a float,
             * even when its pointee is float (e.g. `float *`). Only a by-value
             * float scalar uses the float register/FSTORE path; conflating the
             * two truncated pointer arguments through FSTORE/FLOAD. The pointee
             * float-ness is preserved separately on the symbol (is_float) and
             * consumed when dereferencing. */
            f->arg_float[f->argc] = (uint8_t)(dt.isf && !ptr);
            f->arg_width[f->argc] = c->syms[sym].width;
            f->arg_addr[f->argc] = c->syms[sym].address;
            f->argc++;
        } while (take(c, T_COMMA));
        if (!expect(c, T_RP, "expected )")) {
            return 0;
        }
    }
    if (take(c, T_SEMI)) {
        scope_leave(c);
        return 1;
    }
    f->body = block(c);
    scope_leave(c);
    if (f->body < 0) {
        return 0;
    }
    return 1;
}

static int parse_decls(Compiler *c) {
    int main_i = -1;
    while (cur(c)->kind != T_EOF) {
        uint8_t ret;
        uint8_t gw = 4;
        uint8_t isf = 0;
        uint8_t ptr = 0;
        Token *name;
        uint8_t uns;
        maybe_yield();
        uns = take_qualifiers(c);
        if (take(c, T_ENUM)) {
            take(c, T_ID);
            if (!expect(c, T_LB, "expected { after enum")) {
                return -1;
            }
            if (!parse_enum_list(c)) {
                return -1;
            }
            take(c, T_ID);
            if (!expect(c, T_SEMI, "expected ; after enum")) {
                return -1;
            }
            continue;
        }
        if (take(c, T_TYPEDEF)) {
            DeclType dt;
            Token *tn;
            Token tmp;
            char tname[NAME_MAX];
            tname[0] = 0;
            skip_attr(c);
            if (!parse_type_n(c, &dt, tname, NAME_MAX)) {
                fail(c, cur(c)->line, cur(c)->column, "expected typedef type");
                return -1;
            }
            if (tname[0]) {
                tmp.kind = T_ID;
                tmp.line = cur(c)->line;
                tmp.column = cur(c)->column;
                tmp.value = 0;
                text(tmp.name, NAME_MAX, tname);
                tn = &tmp;
            } else {
                tn = cur(c);
                if (!expect(c, T_ID, "expected typedef name")) {
                    return -1;
                }
            }
            if (take(c, T_LBRACK)) {
                int n = 1;
                uint16_t ew;
                if (!parse_array_dim(c, &n)) {
                    return -1;
                }
                if (n < 1) {
                    n = 1;
                }
                ew = dt.ptr ? 8u : (dt.arr_n > 1 ? dt.w : (dt.w ? dt.w : 4u));
                dt.elemw = ew;
                dt.arr_n = (uint16_t)n;
                dt.w = (uint16_t)(ew * (uint16_t)n);
                dt.ptr = 0;
            }
            if (!expect(c, T_SEMI, "expected ; after typedef")) {
                return -1;
            }
            {
                int ti = typedef_find(c, tn->name);
                if (ti < 0 && c->ntypedef < TYPEDEF_MAX) {
                    ti = c->ntypedef++;
                    text(c->td_name[ti], NAME_MAX, tn->name);
                    ht_ins_grid(c->ht_td, HT_TD_N, tn->name, ti, c->td_name[0],
                                NAME_MAX);
                }
                if (ti >= 0) {
                    c->td_ptr[ti] = dt.ptr;
                    c->td_struct[ti] = dt.sid;
                    c->td_count[ti] = dt.arr_n;
                    c->td_elemw[ti] = dt.elemw;
                    c->td_width[ti] = dt.ptr ? 8 : dt.w;
                }
            }
            continue;
        }
        {
            DeclType dt;
            char nbuf[NAME_MAX];
            int gsid = -1;
            nbuf[0] = 0;
            if (!parse_type_n(c, &dt, nbuf, NAME_MAX)) {
                fail(c, cur(c)->line, cur(c)->column, "expected function or struct");
                return -1;
            }
            ptr = dt.ptr;
            isf = dt.isf;
            gsid = dt.sid;
            if (ptr) {
                gw = 8;
            } else if (gsid >= 0 && c->structs[gsid].size) {
                {
                    uint16_t sz = c->structs[gsid].size;
                    gw = (uint8_t)(sz > 255 ? 255 : sz);
                }
            } else {
                gw = (uint8_t)(dt.w >= 8 ? 8 : (dt.w ? dt.w : 4));
            }
            if (cur(c)->kind == T_SEMI) {
                if (nbuf[0]) {
                    Token vn;
                    int gsym;
                    uint8_t pw =
                        (uint8_t)(ptr ? (dt.w ? dt.w : 4) : gw);
                    vn.kind = T_ID;
                    vn.line = cur(c)->line;
                    vn.column = cur(c)->column;
                    vn.value = 0;
                    text(vn.name, NAME_MAX, nbuf);
                    gsym = sym_add(c, &vn, isf, pw, ptr);
                    if (gsym < 0) {
                        return -1;
                    }
                    c->syms[gsym].is_global = 1;
                    c->syms[gsym].is_unsigned = uns;
                    c->syms[gsym].struct_id = (int16_t)gsid;
                }
                ++c->pos;
                continue;
            }
            if (dt.is_void && !ptr) {
                ret = 0;
            } else if (isf) {
                ret = 2;
            } else {
                ret = 1;
            }
            if (nbuf[0]) {
                Token tmp;
                tmp.kind = T_ID;
                tmp.line = cur(c)->line;
                tmp.column = cur(c)->column;
                tmp.value = 0;
                text(tmp.name, NAME_MAX, nbuf);
                name = &c->tokens[c->pos];
                {
                    int gi;
                    if (cur(c)->kind != T_LP) {
                        Token vn = tmp;
                        int gsym;
                        uint8_t pw =
                            (uint8_t)(ptr ? (dt.w ? dt.w : 4) : gw);
                        gsym = sym_add(c, &vn, isf, pw, ptr);
                        if (gsym < 0) {
                            return -1;
                        }
                        c->syms[gsym].is_global = 1;
                        c->syms[gsym].is_unsigned = uns;
                        c->syms[gsym].struct_id = (int16_t)gsid;
                        if (!expect(c, T_SEMI, "expected ; after global")) {
                            return -1;
                        }
                        continue;
                    }
                    if (!parse_function(c, ret, &tmp, (int16_t)gsid)) {
                        return -1;
                    }
                    (void)gi;
                    continue;
                }
            }
            name = cur(c);
            if (!expect(c, T_ID, "expected function name")) {
                return -1;
            }
            if (cur(c)->kind != T_LP) {
                int gsym;
                uint8_t pw = (uint8_t)(ptr ? (dt.w ? dt.w : 4) : gw);
                for (;;) {
                    int is_arr = 0;
                    int unsized = 0;
                    int count = 1;
                    if (take(c, T_LBRACK)) {
                        is_arr = 1;
                        if (take(c, T_RBRACK)) {
                            unsized = 1;
                            count = 256;
                        } else if (!parse_array_dim(c, &count)) {
                            return -1;
                        }
                    }
                    if (unsized && cur(c)->kind == T_ASSIGN) {
                        int saved = c->pos;
                        ++c->pos;
                        if (cur(c)->kind == T_LB) {
                            int n = count_brace_inits(c);
                            if (n > 0) {
                                count = n;
                            }
                        }
                        c->pos = saved;
                    }
                    if (is_arr) {
                        gsym = sym_add_array(c, name, count, isf, gw == 1 && !ptr,
                                             ptr ? 8 : gw);
                    } else {
                        gsym = sym_add(c, name, isf, pw, ptr);
                    }
                    if (gsym < 0) {
                        return -1;
                    }
                    c->syms[gsym].is_global = 1;
                    c->syms[gsym].is_unsigned = uns;
                    c->syms[gsym].struct_id = (int16_t)gsid;
                    if (gsid >= 0 && c->structs[gsid].size && !ptr) {
                        uint16_t sz = c->structs[gsid].size;
                        c->syms[gsym].stride = sz;
                        if (!is_arr) {
                            if (sz < 256) {
                                c->syms[gsym].width = (uint8_t)sz;
                            }
                        }
                    }
                    if (take(c, T_ASSIGN)) {
                        if (!parse_ginit_for_sym(c, gsym)) {
                            return -1;
                        }
                    }
                    if (!take(c, T_COMMA)) {
                        break;
                    }
                    while (take(c, T_STAR)) {
                        ptr = 1;
                        gw = 8;
                    }
                    name = cur(c);
                    if (!expect(c, T_ID, "expected identifier")) {
                        return -1;
                    }
                }
                if (!expect(c, T_SEMI, "expected ; after global")) {
                    return -1;
                }
                continue;
            }
            if (!parse_function(c, ret, name, (int16_t)gsid)) {
                return -1;
            }
            if (c->funcs[c->nfuncs - 1].is_main) {
                main_i = c->nfuncs - 1;
            }
        }
    }
    (void)main_i;
    return 1;
}

static int program(Compiler *c) {
    int main_i = -1;
    int i;
    if (parse_decls(c) != 1) {
        return -1;
    }
    for (i = 0; i < c->nfuncs; ++i) {
        if (c->funcs[i].is_main) {
            main_i = i;
            break;
        }
    }
    if (main_i < 0) {
        fail(c, 1, 1, "expected main");
        return -1;
    }
    return c->funcs[main_i].body;
}

static int room(Compiler *c, size_t n, Node *x) {
    if (c->pc + n <= c->cap && c->pc + n <= (size_t)CLVM_MAX_CODE) {
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
    uint8_t op32 = op;
    if (op == CL_OP_JMP)
        op32 = CL_OP_JMP32;
    else if (op == CL_OP_JZ)
        op32 = CL_OP_JZ32;
    else if (op == CL_OP_JNZ)
        op32 = CL_OP_JNZ32;
    else if (op == CL_OP_CALL)
        op32 = CL_OP_CALL32;
    if (op32 == CL_OP_JMP32 || op32 == CL_OP_JZ32 || op32 == CL_OP_JNZ32 ||
        op32 == CL_OP_CALL32) {
        if (!room(c, 5, x)) {
            return -1;
        }
        c->out[c->pc++] = op32;
        c->out[c->pc++] = 0;
        c->out[c->pc++] = 0;
        c->out[c->pc++] = 0;
        c->out[c->pc++] = 0;
        return p;
    }
    if (!room(c, 3, x)) {
        return -1;
    }
    c->out[c->pc++] = op;
    c->out[c->pc++] = 0;
    c->out[c->pc++] = 0;
    return p;
}

static int patch(Compiler *c, int p, size_t target, Node *x) {
    uint8_t op = c->out[p];
    int32_t rel;
    uint32_t u;
    if (op == CL_OP_JMP32 || op == CL_OP_JZ32 || op == CL_OP_JNZ32 ||
        op == CL_OP_CALL32) {
        rel = (int32_t)target - (p + 5);
        u = (uint32_t)rel;
        c->out[p + 1] = (uint8_t)u;
        c->out[p + 2] = (uint8_t)(u >> 8);
        c->out[p + 3] = (uint8_t)(u >> 16);
        c->out[p + 4] = (uint8_t)(u >> 24);
        return 1;
    }
    rel = (int32_t)target - (p + 3);
    if (rel < -32768 || rel > 32767) {
        return fail(c, x->line, x->column, "branch too far");
    }
    c->out[p + 1] = (uint8_t)rel;
    c->out[p + 2] = (uint8_t)((uint32_t)rel >> 8);
    return 1;
}

static int gen_expr(Compiler *c, int id);
static int gen_stmt(Compiler *c, int id);

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

static int gen_index_addr(Compiler *c, int sym, int idx_id, Node *n);
static int gen_field_addr(Compiler *c, int sym, int fi, int idx_id, Node *n);
static int expr_ptr_stride(Compiler *c, int nid);

static int incdec_step(Compiler *c, int lhs) {
    int st = expr_ptr_stride(c, lhs);
    return st > 0 ? st : 1;
}

/* Emit ++/-- for a memory lvalue (field/arrow/deref). Leaves old (post) or
 * new (pre) value on the stack. */
static int gen_mem_incdec(Compiler *c, int target, int is_inc, int is_post,
                          Node *n) {
    Node *t = &c->nodes[target];
    uint8_t w = 4;
    uint8_t ld = CL_OP_LOAD;
    uint8_t st = CL_OP_STORE;
    int sid;
    int fi;

    if (t->kind == N_ARROW) {
        sid = c->syms[t->value].struct_id;
        fi = t->left;
        if (sid >= 0 && fi >= 0 && fi < c->structs[sid].nfields)
            w = (uint8_t)c->structs[sid].fwidth[fi];
        if (t->third > 0 && (t->third & 15) != 0)
            w = (uint8_t)(t->third & 15);
        if (gen_field_addr(c, t->value, t->left, -3, t) != 1)
            return -1;
    } else if (t->kind == N_FIELD) {
        sid = c->syms[t->value].struct_id;
        fi = t->left;
        if (sid >= 0 && fi >= 0 && fi < c->structs[sid].nfields)
            w = (uint8_t)c->structs[sid].fwidth[fi];
        if (t->third > 0 && (t->third & 15) != 0)
            w = (uint8_t)(t->third & 15);
        if (gen_field_addr(c, t->value, t->left, t->right, t) != 1)
            return -1;
    } else if (t->kind == N_DEREF) {
        if (gen_expr(c, t->left) != 1)
            return -1;
    } else {
        return fail(c, n->line, n->column, "bad ++/-- lvalue");
    }
    if (w >= 8) {
        ld = CL_OP_LOAD64;
        st = CL_OP_STORE64;
    } else if (w == 1) {
        ld = CL_OP_LOADB;
        st = CL_OP_STOREB;
    }
    if (!byte(c, ld, n))
        return -1;
    {
        int step = incdec_step(c, target);
        if (is_post) {
            if (!byte(c, CL_OP_DUP, n) || !push(c, step, n) ||
                !byte(c, is_inc ? CL_OP_ADD : CL_OP_SUB, n) ||
                !byte(c, CL_OP_DUP, n))
                return -1;
            if (t->kind == N_ARROW) {
                if (gen_field_addr(c, t->value, t->left, -3, t) != 1)
                    return -1;
            } else if (t->kind == N_FIELD) {
                if (gen_field_addr(c, t->value, t->left, t->right, t) != 1)
                    return -1;
            } else if (gen_expr(c, t->left) != 1) {
                return -1;
            }
            if (!byte(c, st, n) || !byte(c, CL_OP_DROP, n))
                return -1;
            return 1;
        }
        if (!push(c, step, n) || !byte(c, is_inc ? CL_OP_ADD : CL_OP_SUB, n) ||
            !byte(c, CL_OP_DUP, n))
            return -1;
    }
    if (t->kind == N_ARROW) {
        if (gen_field_addr(c, t->value, t->left, -3, t) != 1)
            return -1;
    } else if (t->kind == N_FIELD) {
        if (gen_field_addr(c, t->value, t->left, t->right, t) != 1)
            return -1;
    } else if (gen_expr(c, t->left) != 1) {
        return -1;
    }
    if (!byte(c, st, n))
        return -1;
    return 1;
}

static int gen_index_addr(Compiler *c, int sym, int idx_id, Node *n) {
    Symbol *s = &c->syms[sym];
    int stride = s->stride ? (int)s->stride : 1;
    if (gen_expr(c, idx_id) != 1) {
        return -1;
    }
    if (stride != 1) {
        if (!push(c, stride, n) || !byte(c, CL_OP_MUL, n)) {
            return -1;
        }
    }
    /* Arrays: base is the symbol address. Pointers: base is the loaded value. */
    if (s->is_ptr && !s->is_array) {
        if (!push(c, (int32_t)s->address, n) || !byte(c, CL_OP_LOAD64, n) ||
            !byte(c, CL_OP_ADD, n)) {
            return -1;
        }
    } else {
        if (!push(c, (int32_t)s->address, n) || !byte(c, CL_OP_ADD, n)) {
            return -1;
        }
    }
    return 1;
}

static int gen_field_addr(Compiler *c, int sym, int fi, int idx_id, Node *n) {
    uint16_t off = 0;
    int sid = c->syms[sym].struct_id;
    if (sid >= 0 && fi >= 0 && fi < c->structs[sid].nfields) {
        off = c->structs[sid].foff[fi];
    }
    if ((n->kind == N_FIELD || n->kind == N_ARROW) && n->third > 15) {
        off = (uint16_t)(off + (uint16_t)(n->third >> 4));
    }
    if (n->kind == N_FIELD_ASSIGN) {
        off = (uint16_t)(off + (uint16_t)((unsigned)n->third >> 16));
    }
    if (idx_id == -3) {
        if (!push(c, (int32_t)c->syms[sym].address, n) ||
            !byte(c, CL_OP_LOAD64, n)) {
            return -1;
        }
        if (off != 0 && (!push(c, (int32_t)off, n) || !byte(c, CL_OP_ADD, n))) {
            return -1;
        }
        return 1;
    }
    if (idx_id >= 0 && c->nodes[idx_id].kind != N_INDEX) {
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

    if (same(n->name, "va_start")) {
        int ap;
        if (n->value < 1) {
            fail(c, n->line, n->column, "va_start needs an ap");
            return -1;
        }
        ap = c->args[n->left];
        if (c->nodes[ap].kind != N_VAR) {
            fail(c, n->line, n->column, "va_start needs a va_list variable");
            return -1;
        }
        return (push(c, (int32_t)c->va_base, n) &&
                push(c, c->syms[c->nodes[ap].value].address, n) &&
                byte(c, CL_OP_STORE64, n)) ? 0 : -1;
    }
    if (same(n->name, "va_end")) {
        return 0;
    }
    if (same(n->name, "va_arg")) {
        int ap;
        int sy;
        if (n->value < 1) {
            fail(c, n->line, n->column, "va_arg needs an ap");
            return -1;
        }
        ap = c->args[n->left];
        if (c->nodes[ap].kind != N_VAR) {
            fail(c, n->line, n->column, "va_arg needs a va_list variable");
            return -1;
        }
        sy = c->nodes[ap].value;
        if (!push(c, c->syms[sy].address, n) || !byte(c, CL_OP_LOAD64, n) ||
            !byte(c, CL_OP_DUP, n) || !push(c, 8, n) || !byte(c, CL_OP_ADD, n) ||
            !push(c, c->syms[sy].address, n) || !byte(c, CL_OP_STORE64, n) ||
            !byte(c, CL_OP_LOAD64, n)) {
            return -1;
        }
        return 1;
    }
    if (!b) {
        if (same(n->name, "loadb")) {
            if (n->value != 1) {
                fail(c, n->line, n->column, "wrong argument count");
                return -1;
            }
            if (gen_expr(c, c->args[n->left]) != 1) {
                return -1;
            }
            return byte(c, CL_OP_LOADB, n) ? 1 : -1;
        }
        if (same(n->name, "storeb")) {
            if (n->value != 2) {
                fail(c, n->line, n->column, "wrong argument count");
                return -1;
            }
            if (gen_expr(c, c->args[n->left + 1]) != 1) {
                return -1;
            }
            if (gen_expr(c, c->args[n->left]) != 1) {
                return -1;
            }
            return byte(c, CL_OP_STOREB, n) ? 0 : -1;
        }
        fn = func_find(c, n->name);
        if (n->right == -2) {
            for (i = 0; i < n->value; ++i) {
                leaves = gen_expr(c, c->args[n->left + i]);
                if (leaves != 1) {
                    fail(c, n->line, n->column, "argument has no value");
                    return -1;
                }
                if (i < FUNC_ARG_MAX) {
                    if (!push(c, (int32_t)c->icall_base + i * 8, n) ||
                        !byte(c, CL_OP_STORE64, n)) {
                        return -1;
                    }
                } else if (!push(c, (int32_t)c->va_base + (i - FUNC_ARG_MAX) * 8, n) ||
                           !byte(c, CL_OP_STORE64, n)) {
                    return -1;
                }
            }
            if (gen_expr(c, n->third) != 1 || !byte(c, CL_OP_CALLI, n)) {
                return -1;
            }
            return 1;
        }
        if (fn < 0) {
            int sy = n->right > 0 ? n->right - 1 : -1;
            if (sy < 0) {
                char msg[80];
                int k = 0;
                const char *p = "unknown function ";
                while (p[k]) {
                    msg[k] = p[k];
                    k++;
                }
                {
                    int j = 0;
                    while (n->name[j] && k + 1 < 80) {
                        msg[k++] = n->name[j++];
                    }
                }
                msg[k] = 0;
                fail(c, n->line, n->column, msg);
                return -1;
            }
            for (i = 0; i < n->value; ++i) {
                leaves = gen_expr(c, c->args[n->left + i]);
                if (leaves != 1) {
                    fail(c, n->line, n->column, "argument has no value");
                    return -1;
                }
                if (i < FUNC_ARG_MAX) {
                    if (!push(c, (int32_t)c->icall_base + i * 8, n) ||
                        !byte(c, CL_OP_STORE64, n)) {
                        return -1;
                    }
                } else if (!push(c, (int32_t)c->va_base + (i - FUNC_ARG_MAX) * 8, n) ||
                           !byte(c, CL_OP_STORE64, n)) {
                    return -1;
                }
            }
            if (!push(c, c->syms[sy].address, n) || !byte(c, CL_OP_LOAD64, n) ||
                !byte(c, CL_OP_CALLI, n)) {
                return -1;
            }
            return 1;
        }
        if (c->funcs[fn].is_varargs) {
            if (n->value < c->funcs[fn].argc) {
                fail(c, n->line, n->column, "wrong argument count");
                return -1;
            }
        } else if (n->value != c->funcs[fn].argc) {
            fail(c, n->line, n->column, "wrong argument count");
            return -1;
        }
        for (i = 0; i < n->value; ++i) {
            leaves = gen_expr(c, c->args[n->left + i]);
            if (leaves != 1) {
                fail(c, n->line, n->column, "argument has no value");
                return -1;
            }
            if (i < c->funcs[fn].argc) {
                if (!push(c, (int32_t)c->icall_base + i * 8, n) ||
                    !byte(c, c->funcs[fn].arg_float[i] ? CL_OP_FSTORE : CL_OP_STORE64, n)) {
                    return -1;
                }
            } else if (!push(c, (int32_t)c->va_base + (i - c->funcs[fn].argc) * 8, n) ||
                       !byte(c, CL_OP_STORE64, n)) {
                return -1;
            }
        }
        if (emit_call(c, fn, n) != 1) {
            return -1;
        }
        n->is_float = (uint8_t)(c->funcs[fn].ret == 2);
        /* Always leave one stack slot: void callees push 0 before RET. */
        return 1;
    }
    if (b && same(n->name, "fopen") && n->value >= 1) {
        if (gen_expr(c, c->args[n->left]) != 1) {
            return -1;
        }
        for (i = 1; i < n->value; ++i) {
            if (gen_expr(c, c->args[n->left + i]) != 1 || !byte(c, CL_OP_DROP, n)) {
                return -1;
            }
        }
        if (!push(c, b->id, n) || !byte(c, CL_OP_SYS, n)) {
            return -1;
        }
        return 1;
    }
    if (b && (same(n->name, "fread") || same(n->name, "fwrite")) &&
        n->value == 4) {
        if (gen_expr(c, c->args[n->left + 3]) != 1) {
            return -1;
        }
        if (gen_expr(c, c->args[n->left]) != 1) {
            return -1;
        }
        if (gen_expr(c, c->args[n->left + 1]) != 1) {
            return -1;
        }
        if (gen_expr(c, c->args[n->left + 2]) != 1) {
            return -1;
        }
        if (!byte(c, CL_OP_MUL, n)) {
            return -1;
        }
        if (!push(c, b->id, n) || !byte(c, CL_OP_SYS, n)) {
            return -1;
        }
        return 1;
    }
    if (b && same(n->name, "fseek") && n->value == 3) {
        if (gen_expr(c, c->args[n->left]) != 1) {
            return -1;
        }
        if (gen_expr(c, c->args[n->left + 1]) != 1) {
            return -1;
        }
        if (gen_expr(c, c->args[n->left + 2]) != 1 || !byte(c, CL_OP_DROP, n)) {
            return -1;
        }
        if (!push(c, b->id, n) || !byte(c, CL_OP_SYS, n)) {
            return -1;
        }
        return 1;
    }
    if (b && same(n->name, "mkdir") && n->value >= 1) {
        if (gen_expr(c, c->args[n->left]) != 1) {
            return -1;
        }
        for (i = 1; i < n->value; ++i) {
            if (gen_expr(c, c->args[n->left + i]) != 1 || !byte(c, CL_OP_DROP, n)) {
                return -1;
            }
        }
        if (!push(c, b->id, n) || !byte(c, CL_OP_SYS, n)) {
            return -1;
        }
        return 1;
    }
    if (b && same(n->name, "unlink") && n->value >= 1) {
        if (gen_expr(c, c->args[n->left]) != 1) {
            return -1;
        }
        for (i = 1; i < n->value; ++i) {
            if (gen_expr(c, c->args[n->left + i]) != 1 || !byte(c, CL_OP_DROP, n)) {
                return -1;
            }
        }
        if (!push(c, b->id, n) || !byte(c, CL_OP_SYS, n)) {
            return -1;
        }
        return 1;
    }
    if (n->value != b->argc) {
        {
            char msg[96];
            int k = 0;
            const char *p = "wrong builtin argc ";
            while (*p && k + 1 < (int)sizeof(msg))
                msg[k++] = *p++;
            p = n->name;
            while (*p && k + 1 < (int)sizeof(msg))
                msg[k++] = *p++;
            msg[k] = 0;
            fail(c, n->line, n->column, msg);
        }
        return -1;
    }
    for (i = 0; i < n->value; ++i) {
        leaves = gen_expr(c, c->args[n->left + i]);
        if (leaves != 1) {
            fail(c, n->line, n->column, "argument has no value");
            return -1;
        }
        if (b->ret_float && !c->nodes[c->args[n->left + i]].is_float &&
            !byte(c, CL_OP_ITOF, n)) {
            return -1;
        }
    }
    if (!push(c, b->id, n) || !byte(c, CL_OP_SYS, n)) {
        return -1;
    }
    if (b->ret_float) {
        n->is_float = 1;
    }
    return b->returns ? 1 : 0;
}

static int gen_narrow(Compiler *c, Node *n, int width, int uns) {
    int bits;
    if (width <= 0 || width >= 8) {
        return 1;
    }
    bits = 64 - 8 * width;
    if (!push(c, bits, n) || !byte(c, CL_OP_SHL, n)) {
        return 0;
    }
    if (!push(c, bits, n) || !byte(c, uns ? CL_OP_SHR : CL_OP_SAR, n)) {
        return 0;
    }
    return 1;
}

static int gen_load_w(Compiler *c, Node *n, int w) {
    if (w >= 8) {
        return byte(c, CL_OP_LOAD64, n) ? 1 : -1;
    }
    if (w <= 1) {
        return byte(c, CL_OP_LOADB, n) ? 1 : -1;
    }
    if (w == 2) {
        if (!byte(c, CL_OP_DUP, n) || !byte(c, CL_OP_LOADB, n) ||
            !byte(c, CL_OP_SWAP, n) || !push(c, 1, n) ||
            !byte(c, CL_OP_ADD, n) || !byte(c, CL_OP_LOADB, n) ||
            !push(c, 8, n) || !byte(c, CL_OP_SHL, n) ||
            !byte(c, CL_OP_OR, n)) {
            return -1;
        }
        return gen_narrow(c, n, 2, 0) ? 1 : -1;
    }
    return byte(c, CL_OP_LOAD, n) ? 1 : -1;
}

static int expr_ptr_stride(Compiler *c, int nid) {
    Node *n;
    if (nid < 0) {
        return 0;
    }
    n = &c->nodes[nid];
    if (n->kind == N_VAR) {
        Symbol *s = &c->syms[n->value];
        if (s->is_ptr) {
            return s->stride ? (int)s->stride : 1;
        }
        if (s->is_array) {
            return s->stride ? (int)s->stride : 1;
        }
    }
    if (n->kind == N_CAST && (n->value & CAST_PTR)) {
        int pw = CAST_POINTEE(n->value);
        if (pw <= 1) {
            return 1;
        }
        return pw;
    }
    if (n->kind == N_PTRFIELD) {
        int w = n->value & 0xff;
        if (w == 8 && n->sid >= 0 && n->sid < c->nstructs &&
            c->structs[n->sid].size)
            return (int)c->structs[n->sid].size;
    }
    if ((n->kind == N_ARROW || n->kind == N_FIELD) && n->value >= 0 &&
        n->value < c->nsyms) {
        int sid = c->syms[n->value].struct_id;
        int fi = n->left;
        if (sid >= 0 && fi >= 0 && fi < c->structs[sid].nfields &&
            c->structs[sid].fptr[fi]) {
            int fs = c->structs[sid].fstruct[fi];
            if (fs >= 0 && c->structs[fs].size)
                return (int)c->structs[fs].size;
            return 4;
        }
    }
    if (n->kind == N_ADD || n->kind == N_SUB) {
        int sl = expr_ptr_stride(c, n->left);
        if (sl) {
            return sl;
        }
        return expr_ptr_stride(c, n->right);
    }
    if (n->kind == N_ADDR) {
        return expr_ptr_stride(c, n->left);
    }
    return 0;
}

static int gen_expr(Compiler *c, int id) {
    Node *n = &c->nodes[id];
    int a;
    int b;
    uint8_t op = 0;

    if (n->kind == N_INT) {
        return push(c, n->value, n) ? 1 : -1;
    }
    if (n->kind == N_FNPTR) {
        if (c->nfnptr == FNPTR_MAX || !push(c, 0, n)) {
            return -1;
        }
        c->fnptr_at[c->nfnptr] = (int)c->pc - 4;
        c->fnptr_fn[c->nfnptr] = n->value;
        c->nfnptr++;
        return 1;
    }
    if (n->kind == N_FLOAT) {
        return fpush(c, n->value, n) ? 1 : -1;
    }
    if (n->kind == N_STR) {
        return push(c, (int32_t)c->str_base + n->value, n) ? 1 : -1;
    }
    if (n->kind == N_VAR) {
        if (c->syms[n->value].is_tls) {
            return push(c, (int32_t)c->syms[n->value].tls_i, n) &&
                   push(c, 131, n) && byte(c, CL_OP_SYS, n) ? 1 : -1;
        }
        if (c->syms[n->value].is_array) {
            return push(c, (int32_t)c->syms[n->value].address, n) ? 1 : -1;
        }
        if (n->is_float) {
            return push(c, c->syms[n->value].address, n) &&
                   byte(c, CL_OP_FLOAD, n) ? 1 : -1;
        }
        if (!push(c, c->syms[n->value].address, n) ||
            !byte(c, mem_ld(&c->syms[n->value]), n)) {
            return -1;
        }
        if (c->syms[n->value].is_unsigned && c->syms[n->value].width == 1) {
            if (!push(c, 255, n) || !byte(c, CL_OP_AND, n)) {
                return -1;
            }
        } else if (c->syms[n->value].is_unsigned && c->syms[n->value].width == 2) {
            if (!push(c, 65535, n) || !byte(c, CL_OP_AND, n)) {
                return -1;
            }
        }
        return 1;
    }
    if (n->kind == N_INDEX) {
        if (gen_index_addr(c, n->value, n->left, n) != 1) {
            return -1;
        }
        if (c->syms[n->value].packed ||
            (c->syms[n->value].is_ptr && c->syms[n->value].pointee == 1)) {
            return byte(c, CL_OP_LOADB, n) ? 1 : -1;
        }
        if (c->syms[n->value].is_ptr && c->syms[n->value].pointee == 2) {
            return gen_load_w(c, n, 2);
        }
        if (c->syms[n->value].is_float) {
            return byte(c, CL_OP_FLOAD, n) ? 1 : -1;
        }
        if ((!c->syms[n->value].is_ptr && c->syms[n->value].width >= 8) ||
            (c->syms[n->value].is_ptr && c->syms[n->value].pointee >= 8)) {
            return byte(c, CL_OP_LOAD64, n) ? 1 : -1;
        }
        return byte(c, CL_OP_LOAD, n) ? 1 : -1;
    }
    if (n->kind == N_FIELD) {
        int sid;
        uint8_t w = 4;
        int elemw = 0;
        sid = c->syms[n->value].struct_id;
        if (sid >= 0 && n->left >= 0 && n->left < c->structs[sid].nfields)
            elemw = (int)c->structs[sid].felemw[n->left];
        if (!n->is_float && elemw == 0 && n->right < 0 && (n->third >> 4) == 0 &&
            sid >= 0 && n->left >= 0 && n->left < c->structs[sid].nfields &&
            c->structs[sid].fbits[n->left] == 0 &&
            c->structs[sid].fwidth[n->left] >= 8) {
            uint32_t u = c->structs[sid].foff[n->left];
            if (!push(c, (int32_t)c->syms[n->value].address, n) ||
                !room(c, 5, n))
                return -1;
            c->out[c->pc++] = CL_OP_LDFLD;
            c->out[c->pc++] = (uint8_t)u;
            c->out[c->pc++] = (uint8_t)(u >> 8);
            c->out[c->pc++] = (uint8_t)(u >> 16);
            c->out[c->pc++] = (uint8_t)(u >> 24);
            return 1;
        }
        if (gen_field_addr(c, n->value, n->left, n->right, n) != 1) {
            return -1;
        }
        /* Embedded array decays to pointer. N_FIELD.right is a struct-array
         * index (lumpinfo[i].name), not an index into the char[]. Element
         * access is N_PTRFIELD. */
        if (elemw > 0)
            return 1;
        if (n->third > 0) {
            w = (uint8_t)(n->third & 15);
            if (w == 0) {
                w = 4;
            }
        } else if (sid >= 0 && n->left >= 0 && n->left < c->structs[sid].nfields) {
            w = c->structs[sid].fwidth[n->left];
        }
        if (sid >= 0 && n->left >= 0 && n->left < c->structs[sid].nfields &&
            c->structs[sid].fbits[n->left] > 0) {
            int bits = c->structs[sid].fbits[n->left];
            int boff = c->structs[sid].fbitoff[n->left];
            int mask = (bits >= 31) ? -1 : ((1 << bits) - 1);
            if (gen_load_w(c, n, 4) != 1)
                return -1;
            if (boff && (!push(c, boff, n) || !byte(c, CL_OP_SHR, n)))
                return -1;
            if (!push(c, mask, n) || !byte(c, CL_OP_AND, n))
                return -1;
            return 1;
        }
        if (n->is_float) {
            return byte(c, CL_OP_FLOAD, n) ? 1 : -1;
        }
        return gen_load_w(c, n, w);
    }
    if (n->kind == N_ARROW) {
        int sid;
        uint8_t w = 4;
        int elemw = 0;
        sid = c->syms[n->value].struct_id;
        if (sid >= 0 && n->left >= 0 && n->left < c->structs[sid].nfields)
            elemw = (int)c->structs[sid].felemw[n->left];
        if (gen_field_addr(c, n->value, n->left, -3, n) != 1) {
            return -1;
        }
        /* Embedded array: lvalue/rvalue decay to pointer — leave address. */
        if (elemw > 0 && n->right < 0)
            return 1;
        if (n->right >= 0) {
            int stride = elemw > 0 ? elemw : 4;
            if (gen_expr(c, n->right) != 1) {
                return -1;
            }
            if (!push(c, stride, n) || !byte(c, CL_OP_MUL, n) ||
                !byte(c, CL_OP_ADD, n)) {
                return -1;
            }
            w = (uint8_t)(stride < 256 ? stride : 4);
        } else {
            sid = c->syms[n->value].struct_id;
            if (n->third > 0) {
                w = (uint8_t)(n->third & 15);
                if (w == 0) {
                    w = 4;
                }
            } else if (sid >= 0 && n->left >= 0 &&
                       n->left < c->structs[sid].nfields) {
                w = c->structs[sid].fwidth[n->left];
            }
        }
        if (n->is_float) {
            return byte(c, CL_OP_FLOAD, n) ? 1 : -1;
        }
        return gen_load_w(c, n, w);
    }
    if (n->kind == N_PTRFIELD) {
        uint8_t w = 4;
        {
            Node *L = &c->nodes[n->left];
            if (L->kind == N_FIELD && n->right >= 0) {
                if (gen_field_addr(c, L->value, L->left, L->right, n) != 1) {
                    return -1;
                }
                if ((L->third >> 4) != 0 &&
                    (!push(c, L->third >> 4, n) || !byte(c, CL_OP_ADD, n))) {
                    return -1;
                }
            } else if (L->kind == N_ARROW && n->right >= 0) {
                if (gen_field_addr(c, L->value, L->left, -3, n) != 1) {
                    return -1;
                }
            } else if (L->kind == N_PTRFIELD) {
                if (gen_expr(c, n->left) != 1) {
                    return -1;
                }
            } else if (gen_expr(c, n->left) != 1) {
                return -1;
            }
            if ((n->value & 0x10000) && !byte(c, CL_OP_LOAD64, n)) {
                return -1;
            }
        }
        if (n->right >= 0) {
            int stride = n->value & 0xffff;
            if (gen_expr(c, n->right) != 1) {
                return -1;
            }
            if (!push(c, stride ? stride : 4, n) || !byte(c, CL_OP_MUL, n) ||
                !byte(c, CL_OP_ADD, n)) {
                return -1;
            }
            /* third==0: indexed address, no extra field offset.
             * char/byte arrays must load; struct arrays leave the address
             * for `.field` / `&p->arr[i]`. */
            if (n->third == 0) {
                if (stride == 1 || (n->value & 0x20000))
                    return gen_load_w(c, n, 1);
                if (n->sid >= 0)
                    return 1;
                w = (uint8_t)(stride >= 8 ? 8 : (stride ? stride : 4));
                return gen_load_w(c, n, w);
            }
            if (stride == 1 || (n->value & 0x20000))
                w = 1;
            else if (stride >= 8)
                w = 8;
            else if (stride > 0)
                w = (uint8_t)stride;
        } else {
            w = (uint8_t)(n->value & 0xff);
            if (w == 0) {
                w = 4;
            }
        }
        if (n->third != 0 &&
            (!push(c, n->third, n) || !byte(c, CL_OP_ADD, n))) {
            return -1;
        }
        if ((n->value & 0x40000) && n->right < 0) {
            return 1;
        }
        if (n->is_float) {
            return byte(c, CL_OP_FLOAD, n) ? 1 : -1;
        }
        return gen_load_w(c, n, w);
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
    if (n->kind == N_NOT) {
        a = gen_expr(c, n->left);
        if (a != 1) {
            return -1;
        }
        return push(c, 0, n) && byte(c, CL_OP_EQ, n) ? 1 : -1;
    }
    if (n->kind == N_BITNOT) {
        if (gen_expr(c, n->left) != 1) {
            return -1;
        }
        return byte(c, CL_OP_NOT, n) ? 1 : -1;
    }
    if (n->kind == N_ADDR) {
        Node *l = &c->nodes[n->left];
        if (l->kind == N_VAR) {
            return push(c, c->syms[l->value].address, n) ? 1 : -1;
        }
        if (l->kind == N_INDEX) {
            return gen_index_addr(c, l->value, l->left, l);
        }
        if (l->kind == N_FIELD) {
            return gen_field_addr(c, l->value, l->left, l->right, l);
        }
        if (l->kind == N_ARROW) {
            return gen_field_addr(c, l->value, l->left, -3, l);
        }
        if (l->kind == N_DEREF) {
            return gen_expr(c, l->left);
        }
        if (l->kind == N_PTRFIELD) {
            if (gen_expr(c, l->left) != 1) {
                return -1;
            }
            if (l->right >= 0) {
                if (gen_expr(c, l->right) != 1) {
                    return -1;
                }
                if (!push(c, (l->value & 0xffff) ? (l->value & 0xffff) : 4, n) ||
                    !byte(c, CL_OP_MUL, n) ||
                    !byte(c, CL_OP_ADD, n)) {
                    return -1;
                }
            }
            if (l->third != 0 &&
                (!push(c, l->third, n) || !byte(c, CL_OP_ADD, n))) {
                return -1;
            }
            return 1;
        }
        fail(c, n->line, n->column, "& needs lvalue");
        return -1;
    }
    if (n->kind == N_DEREF) {
        uint8_t op = CL_OP_LOAD;
        int src = n->left;
        if (gen_expr(c, n->left) != 1) {
            return -1;
        }
        while (src >= 0 && c->nodes[src].kind == N_CAST) {
            int32_t cv = c->nodes[src].value;
            if (cv & CAST_PTR) {
                int pw = CAST_POINTEE(cv);
                if (cv & CAST_PFL) {
                    op = CL_OP_FLOAD;
                } else if (pw <= 1) {
                    op = CL_OP_LOADB;
                } else if (pw >= 8) {
                    op = CL_OP_LOAD64;
                } else {
                    op = CL_OP_LOAD;
                }
                src = -1;
                break;
            }
            src = c->nodes[src].left;
        }
        if (src >= 0 && c->nodes[src].kind == N_VAR) {
            Symbol *s = &c->syms[c->nodes[src].value];
            if (s->is_ptr) {
                if (s->pointee == 1) {
                    op = CL_OP_LOADB;
                } else if (s->pointee >= 8) {
                    op = CL_OP_LOAD64;
                }
            }
        }
        if (!byte(c, op, n)) {
            return -1;
        }
        if (op == CL_OP_LOADB) {
            int32_t cv = 0;
            int uns = 0;
            if (n->left >= 0) {
                cv = c->nodes[n->left].value;
                uns = (c->nodes[n->left].kind == N_CAST) && (cv & CAST_UNS);
            }
            if (!uns && !gen_narrow(c, n, 1, 0)) {
                return -1;
            }
        }
        return 1;
    }
    if (n->kind == N_ASSIGN) {
        int v = gen_expr(c, n->left);
        if (v != 1 || !byte(c, CL_OP_DUP, n) ||
            !push(c, c->syms[n->value].address, n) ||
            !byte(c, mem_st(&c->syms[n->value]), n)) {
            return -1;
        }
        return 1;
    }
    if (n->kind == N_INDEX_ASSIGN) {
        uint8_t op = mem_st(&c->syms[n->value]);
        if (gen_expr(c, n->left) != 1) {
            return -1;
        }
        if (!byte(c, CL_OP_DUP, n)) {
            return -1;
        }
        if (gen_index_addr(c, n->value, n->right, n) != 1) {
            return -1;
        }
        if (c->syms[n->value].packed ||
            (c->syms[n->value].is_ptr && c->syms[n->value].pointee == 1)) {
            op = CL_OP_STOREB;
        } else if (c->syms[n->value].is_ptr && c->syms[n->value].pointee >= 8) {
            op = CL_OP_STORE64;
        }
        return byte(c, op, n) ? 1 : -1;
    }
    if (n->kind == N_FIELD_ASSIGN) {
        int sid = c->syms[n->value].struct_id;
        uint8_t isf;
        int fi = n->third & 255;
        int fw = (n->third >> 8) & 255;
        uint8_t op;
        if (sid < 0 || fi < 0) {
            fail(c, n->line, n->column, "bad field assign");
            return -1;
        }
        isf = n->is_float;
        if (!isf && sid >= 0 && fi < c->structs[sid].nfields) {
            isf = c->structs[sid].is_float[fi];
        }
        if (fw == 0 && sid >= 0 && fi < c->structs[sid].nfields) {
            fw = c->structs[sid].fwidth[fi];
        }
        if (gen_expr(c, n->left) != 1) {
            return -1;
        }
        if (!isf && sid >= 0 && fi < c->structs[sid].nfields &&
            c->structs[sid].fbits[fi] > 0) {
            int bits = c->structs[sid].fbits[fi];
            int boff = c->structs[sid].fbitoff[fi];
            int mask = (bits >= 31) ? -1 : ((1 << bits) - 1);
            int clear = ~(mask << boff);
            if (!push(c, mask, n) || !byte(c, CL_OP_AND, n) ||
                !byte(c, CL_OP_DUP, n))
                return -1;
            if (boff && (!push(c, boff, n) || !byte(c, CL_OP_SHL, n)))
                return -1;
            if (gen_field_addr(c, n->value, fi, n->right, n) != 1 ||
                !byte(c, CL_OP_LOAD, n) || !push(c, clear, n) ||
                !byte(c, CL_OP_AND, n) || !byte(c, CL_OP_SWAP, n) ||
                !byte(c, CL_OP_OR, n))
                return -1;
            if (gen_field_addr(c, n->value, fi, n->right, n) != 1)
                return -1;
            return byte(c, CL_OP_STORE, n) ? 1 : -1;
        }
        if (!byte(c, CL_OP_DUP, n)) {
            return -1;
        }
        if (gen_field_addr(c, n->value, fi, n->right, n) != 1) {
            return -1;
        }
        if (isf) {
            op = CL_OP_FSTORE;
        } else if (fw >= 8) {
            op = CL_OP_STORE64;
        } else if (fw == 1) {
            op = CL_OP_STOREB;
        } else if (fw == 2) {
            /* [val][val][addr] STOREB low, then high at addr+1. */
            if (!byte(c, CL_OP_STOREB, n) || !byte(c, CL_OP_DUP, n) ||
                !push(c, 8, n) || !byte(c, CL_OP_SHR, n)) {
                return -1;
            }
            if (gen_field_addr(c, n->value, fi, n->right, n) != 1) {
                return -1;
            }
            if (!push(c, 1, n) || !byte(c, CL_OP_ADD, n) ||
                !byte(c, CL_OP_STOREB, n)) {
                return -1;
            }
            return 1;
        } else {
            op = CL_OP_STORE;
        }
        return byte(c, op, n) ? 1 : -1;
    }
    if (n->kind == N_DEREF_ASSIGN) {
        uint8_t op = CL_OP_STORE;
        int src = n->right;
        int sw = n->value;
        if (sw == 1)
            op = CL_OP_STOREB;
        if (gen_expr(c, n->left) != 1) {
            return -1;
        }
        if (!byte(c, CL_OP_DUP, n)) {
            return -1;
        }
        if (sw == 2) {
            if (gen_expr(c, n->right) != 1) {
                return -1;
            }
            if (!byte(c, CL_OP_STOREB, n) || !byte(c, CL_OP_DUP, n) ||
                !push(c, 8, n) || !byte(c, CL_OP_SHR, n)) {
                return -1;
            }
            if (gen_expr(c, n->right) != 1) {
                return -1;
            }
            if (!push(c, 1, n) || !byte(c, CL_OP_ADD, n) ||
                !byte(c, CL_OP_STOREB, n)) {
                return -1;
            }
            return 1;
        }
        if (gen_expr(c, n->right) != 1) {
            return -1;
        }
        if (op == CL_OP_STOREB)
            return byte(c, op, n) ? 1 : -1;
        while (src >= 0 && c->nodes[src].kind == N_CAST) {
            int32_t cv = c->nodes[src].value;
            if (cv & CAST_PTR) {
                int pw = CAST_POINTEE(cv);
                if (pw <= 1) {
                    op = CL_OP_STOREB;
                } else if (pw >= 8) {
                    op = CL_OP_STORE64;
                }
                src = -1;
                break;
            }
            src = c->nodes[src].left;
        }
        if (src >= 0 && c->nodes[src].kind == N_VAR) {
            Symbol *s = &c->syms[c->nodes[src].value];
            if (s->is_ptr && s->pointee == 1) {
                op = CL_OP_STOREB;
            } else if (s->is_ptr && s->pointee >= 8) {
                op = CL_OP_STORE64;
            }
        }
        return byte(c, op, n) ? 1 : -1;
    }
    if (n->kind == N_CAST) {
        int32_t cv = n->value;
        int w = CAST_WIDTH(cv);
        int uns = (cv & CAST_UNS) != 0;
        int ptr = (cv & CAST_PTR) != 0;
        int is_bool = (cv & CAST_BOOL) != 0;
        int is_void = (cv & CAST_VOID) != 0;
        int leaves = gen_expr(c, n->left);
        if (leaves < 0) {
            return -1;
        }
        if (is_void) {
            if (leaves == 1 && !byte(c, CL_OP_DROP, n)) {
                return -1;
            }
            return 0;
        }
        if (leaves != 1) {
            fail(c, n->line, n->column, "cast needs a value");
            return -1;
        }
        if (n->is_float) {
            if (!c->nodes[n->left].is_float && !byte(c, CL_OP_ITOF, n)) {
                return -1;
            }
            return 1;
        }
        if (c->nodes[n->left].is_float && !byte(c, CL_OP_FTOI, n)) {
            return -1;
        }
        if (is_bool) {
            return (push(c, 0, n) && byte(c, CL_OP_NE, n)) ? 1 : -1;
        }
        if (ptr) {
            w = 8;
        }
        return gen_narrow(c, n, w, uns && !ptr) ? 1 : -1;
    }
    if (n->kind == N_COMMA) {
        int leaves = gen_expr(c, n->left);
        if (leaves < 0) {
            return -1;
        }
        if (leaves == 1 && !byte(c, CL_OP_DROP, n)) {
            return -1;
        }
        return gen_expr(c, n->right);
    }
    if (n->kind == N_TERNARY) {
        int p;
        int q;
        if (gen_expr(c, n->left) != 1) {
            return -1;
        }
        p = branch(c, CL_OP_JZ, n);
        if (p < 0 || gen_expr(c, n->right) != 1) {
            return -1;
        }
        q = branch(c, CL_OP_JMP, n);
        if (q < 0 || !patch(c, p, c->pc, n) || gen_expr(c, n->third) != 1 ||
            !patch(c, q, c->pc, n)) {
            return -1;
        }
        return 1;
    }
    if (n->kind == N_PREINC || n->kind == N_PREDEC) {
        Node *lhs = &c->nodes[n->left];
        int sym = n->value;
        if (lhs->kind == N_VAR)
            sym = lhs->value;
        if (lhs->kind == N_ARROW || lhs->kind == N_FIELD ||
            lhs->kind == N_DEREF)
            return gen_mem_incdec(c, n->left, n->kind == N_PREINC, 0, n);
        if (sym < 0 || !push(c, c->syms[sym].address, n) ||
            !byte(c, mem_ld(&c->syms[sym]), n) ||
            !push(c, incdec_step(c, n->left), n) ||
            !byte(c, n->kind == N_PREINC ? CL_OP_ADD : CL_OP_SUB, n) ||
            !byte(c, CL_OP_DUP, n) || !push(c, c->syms[sym].address, n) ||
            !byte(c, mem_st(&c->syms[sym]), n)) {
            return -1;
        }
        return 1;
    }
    if (n->kind == N_POSTINC || n->kind == N_POSTDEC) {
        Node *lhs = &c->nodes[n->left];
        int sym = n->value;
        if (lhs->kind == N_VAR)
            sym = lhs->value;
        if (lhs->kind == N_ARROW || lhs->kind == N_FIELD ||
            lhs->kind == N_DEREF)
            return gen_mem_incdec(c, n->left, n->kind == N_POSTINC, 1, n);
        if (sym < 0 || !push(c, c->syms[sym].address, n) ||
            !byte(c, mem_ld(&c->syms[sym]), n) || !byte(c, CL_OP_DUP, n) ||
            !push(c, incdec_step(c, n->left), n) ||
            !byte(c, n->kind == N_POSTINC ? CL_OP_ADD : CL_OP_SUB, n) ||
            !push(c, c->syms[sym].address, n) ||
            !byte(c, mem_st(&c->syms[sym]), n)) {
            return -1;
        }
        return 1;
    }
    if (n->kind == N_AND) {
        int p;
        if (gen_expr(c, n->left) != 1) {
            return -1;
        }
        if (!byte(c, CL_OP_DUP, n)) {
            return -1;
        }
        p = branch(c, CL_OP_JZ, n);
        if (p < 0 || !byte(c, CL_OP_DROP, n)) {
            return -1;
        }
        if (gen_expr(c, n->right) != 1) {
            return -1;
        }
        return patch(c, p, c->pc, n) ? 1 : -1;
    }
    if (n->kind == N_OR) {
        int p;
        if (gen_expr(c, n->left) != 1) {
            return -1;
        }
        if (!byte(c, CL_OP_DUP, n)) {
            return -1;
        }
        p = branch(c, CL_OP_JNZ, n);
        if (p < 0 || !byte(c, CL_OP_DROP, n)) {
            return -1;
        }
        if (gen_expr(c, n->right) != 1) {
            return -1;
        }
        return patch(c, p, c->pc, n) ? 1 : -1;
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
        case N_MOD: {
            /* a - b * (float)(int)(a / b), trunc toward zero. */
            if (gen_expr(c, n->left) != 1 || !byte(c, CL_OP_STLOC, n) ||
                !byte(c, 30, n))
                return -1;
            if (gen_expr(c, n->right) != 1 || !byte(c, CL_OP_STLOC, n) ||
                !byte(c, 31, n))
                return -1;
            if (!byte(c, CL_OP_LDLOC, n) || !byte(c, 30, n) ||
                !byte(c, CL_OP_LDLOC, n) || !byte(c, 31, n) ||
                !byte(c, CL_OP_FDIV, n) || !byte(c, CL_OP_FTOI, n) ||
                !byte(c, CL_OP_ITOF, n) || !byte(c, CL_OP_LDLOC, n) ||
                !byte(c, 31, n) || !byte(c, CL_OP_FMUL, n) ||
                !byte(c, CL_OP_LDLOC, n) || !byte(c, 30, n) ||
                !byte(c, CL_OP_SWAP, n) || !byte(c, CL_OP_FSUB, n))
                return -1;
            return 1;
        }
        default:
            fail(c, n->line, n->column, "bad expression node");
            return -1;
        }
    }
    if ((n->kind == N_ADD || n->kind == N_SUB) && !n->is_float) {
        int sl = expr_ptr_stride(c, n->left);
        int sr = expr_ptr_stride(c, n->right);
        if (sl && !sr) {
            if (gen_expr(c, n->left) != 1 || gen_expr(c, n->right) != 1) {
                return -1;
            }
            if (!push(c, sl, n) || !byte(c, CL_OP_MUL, n) ||
                !byte(c, n->kind == N_ADD ? CL_OP_ADD : CL_OP_SUB, n)) {
                return -1;
            }
            return 1;
        }
        if (sr && !sl && n->kind == N_ADD) {
            if (gen_expr(c, n->right) != 1 || gen_expr(c, n->left) != 1) {
                return -1;
            }
            if (!push(c, sr, n) || !byte(c, CL_OP_MUL, n) ||
                !byte(c, CL_OP_ADD, n)) {
                return -1;
            }
            return 1;
        }
        if (sl && sr && n->kind == N_SUB && sl == sr) {
            if (gen_expr(c, n->left) != 1 || gen_expr(c, n->right) != 1) {
                return -1;
            }
            if (!byte(c, CL_OP_SUB, n) || !push(c, sl, n) ||
                !byte(c, CL_OP_DIV, n)) {
                return -1;
            }
            return 1;
        }
    }
    a = gen_expr(c, n->left);
    if (a < 0) {
        return -1;
    }
    b = gen_expr(c, n->right);
    if (b < 0) {
        return -1;
    }
    if (a != 1 || b != 1) {
        fail(c, n->line, n->column, "op needs values");
        return -1;
    }
    switch (n->kind) {
    case N_ADD: op = CL_OP_ADD; break;
    case N_SUB: op = CL_OP_SUB; break;
    case N_MUL: op = CL_OP_MUL; break;
    case N_DIV:
        op = CL_OP_DIV;
        if ((c->nodes[n->left].kind == N_VAR &&
             c->syms[c->nodes[n->left].value].is_unsigned) ||
            (c->nodes[n->left].kind == N_CAST &&
             (c->nodes[n->left].value & CAST_UNS))) {
            op = CL_OP_UDIV;
        }
        break;
    case N_MOD:
        op = CL_OP_MOD;
        if ((c->nodes[n->left].kind == N_VAR &&
             c->syms[c->nodes[n->left].value].is_unsigned) ||
            (c->nodes[n->left].kind == N_CAST &&
             (c->nodes[n->left].value & CAST_UNS))) {
            op = CL_OP_UMOD;
        }
        break;
    case N_EQ:  op = CL_OP_EQ;  break;
    case N_NE:  op = CL_OP_NE;  break;
    case N_LT:
        op = CL_OP_LT;
        if ((c->nodes[n->left].kind == N_VAR &&
             c->syms[c->nodes[n->left].value].is_unsigned) ||
            (c->nodes[n->left].kind == N_CAST &&
             (c->nodes[n->left].value & CAST_UNS))) {
            op = CL_OP_ULT;
        }
        break;
    case N_LE:
        op = CL_OP_LE;
        if ((c->nodes[n->left].kind == N_VAR &&
             c->syms[c->nodes[n->left].value].is_unsigned) ||
            (c->nodes[n->left].kind == N_CAST &&
             (c->nodes[n->left].value & CAST_UNS))) {
            op = CL_OP_ULE;
        }
        break;
    case N_GT:
        op = CL_OP_GT;
        if ((c->nodes[n->left].kind == N_VAR &&
             c->syms[c->nodes[n->left].value].is_unsigned) ||
            (c->nodes[n->left].kind == N_CAST &&
             (c->nodes[n->left].value & CAST_UNS))) {
            op = CL_OP_UGT;
        }
        break;
    case N_GE:
        op = CL_OP_GE;
        if ((c->nodes[n->left].kind == N_VAR &&
             c->syms[c->nodes[n->left].value].is_unsigned) ||
            (c->nodes[n->left].kind == N_CAST &&
             (c->nodes[n->left].value & CAST_UNS))) {
            op = CL_OP_UGE;
        }
        break;
    case N_BITAND: op = CL_OP_AND; break;
    case N_BITOR:  op = CL_OP_OR;  break;
    case N_BITXOR: op = CL_OP_XOR; break;
    case N_SHL:    op = CL_OP_SHL; break;
    case N_SHR:
        op = CL_OP_SAR;
        if ((c->nodes[n->left].kind == N_VAR &&
             c->syms[c->nodes[n->left].value].is_unsigned) ||
            (c->nodes[n->left].kind == N_CAST &&
             (c->nodes[n->left].value & CAST_UNS))) {
            op = CL_OP_SHR;
        }
        break;
    default:
        fail(c, n->line, n->column, "bad expression node");
        return -1;
    }
    return byte(c, op, n) ? 1 : -1;
}

static int gen_block(Compiler *c, int id);

static int loop_enter(Compiler *c, Node *n, int is_switch) {
    if (c->loop_sp >= LOOP_MAX) {
        return fail(c, n->line, n->column, "loops nested too deep");
    }
    c->loop_nbrk[c->loop_sp] = 0;
    c->loop_ncont[c->loop_sp] = 0;
    c->loop_switch[c->loop_sp] = (uint8_t)(is_switch ? 1 : 0);
    c->loop_sp++;
    return 1;
}

static int loop_patch_list(Compiler *c, int *list, int n, size_t target, Node *x) {
    int i;
    for (i = 0; i < n; ++i) {
        if (!patch(c, list[i], target, x)) {
            return 0;
        }
    }
    return 1;
}

static int gen_stmt(Compiler *c, int id) {
    Node *n = &c->nodes[id];
    int v;
    int p;
    int q;

    if (c->result && c->result->map_n < CHRIS_MAP_MAX) {
        int mi = c->result->map_n++;
        int fid = 0;
        int orig = n->line;
        line_origin(c, n->line, &fid, &orig);
        if (fid < 0) {
            fid = 0;
        }
        c->result->map[mi].pc = (uint32_t)c->pc;
        c->result->map[mi].line = (uint16_t)orig;
        c->result->map[mi].file_id = (uint16_t)fid;
    }

    if (n->kind == N_BLOCK) {
        return gen_block(c, id);
    }
    if (n->kind == N_DECL) {
        int is_float = c->syms[n->value].is_float;
        if ((c->syms[n->value].struct_id >= 0 || c->syms[n->value].is_array) &&
            n->left < 0) {
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
        if (c->syms[n->value].is_tls) {
            return push(c, (int32_t)c->syms[n->value].tls_i, n) &&
                   push(c, 132, n) && byte(c, CL_OP_SYS, n);
        }
        return push(c, c->syms[n->value].address, n) &&
               byte(c, is_float ? CL_OP_FSTORE : mem_st(&c->syms[n->value]), n);
    }
    if (n->kind == N_ASSIGN) {
        if (c->syms[n->value].struct_id >= 0 && !c->syms[n->value].is_ptr &&
            n->left >= 0 && c->nodes[n->left].kind == N_VAR &&
            c->syms[c->nodes[n->left].value].struct_id ==
                c->syms[n->value].struct_id) {
            int sz = c->structs[c->syms[n->value].struct_id].size;
            int off;
            int32_t src = (int32_t)c->syms[c->nodes[n->left].value].address;
            int32_t dst = (int32_t)c->syms[n->value].address;
            if (sz < 1)
                sz = 4;
            for (off = 0; off + 4 <= sz; off += 4) {
                if (!push(c, src + off, n) || !byte(c, CL_OP_LOAD, n) ||
                    !push(c, dst + off, n) || !byte(c, CL_OP_STORE, n))
                    return 0;
            }
            return 1;
        }
        if (c->syms[n->value].is_tls) {
            v = gen_expr(c, n->left);
            return v == 1 && push(c, (int32_t)c->syms[n->value].tls_i, n) &&
                   push(c, 132, n) && byte(c, CL_OP_SYS, n);
        }
        v = gen_expr(c, n->left);
        return v == 1 && push(c, c->syms[n->value].address, n) &&
               byte(c, mem_st(&c->syms[n->value]), n);
    }
    if (n->kind == N_INDEX_ASSIGN) {
        v = gen_expr(c, n->left);
        if (v != 1) {
            return 0;
        }
        if (gen_index_addr(c, n->value, n->right, n) != 1) {
            return 0;
        }
        if (c->syms[n->value].packed ||
            (c->syms[n->value].is_ptr && c->syms[n->value].pointee == 1)) {
            return byte(c, CL_OP_STOREB, n);
        }
        if (c->syms[n->value].is_ptr && c->syms[n->value].pointee >= 8) {
            return byte(c, CL_OP_STORE64, n);
        }
        return byte(c, mem_st(&c->syms[n->value]), n);
    }
    if (n->kind == N_FIELD_ASSIGN) {
        int sid = c->syms[n->value].struct_id;
        uint8_t isf;
        int fi = n->third & 255;
        int fw = (n->third >> 8) & 255;
        if (sid < 0 || fi < 0) {
            return fail(c, n->line, n->column, "bad field assign");
        }
        isf = n->is_float;
        if (!isf && sid >= 0 && fi < c->structs[sid].nfields) {
            isf = c->structs[sid].is_float[fi];
        }
        if (fw == 0 && sid >= 0 && fi < c->structs[sid].nfields) {
            fw = c->structs[sid].fwidth[fi];
        }
        v = gen_expr(c, n->left);
        if (v != 1) {
            return 0;
        }
        if (!isf && sid >= 0 && fi < c->structs[sid].nfields &&
            c->structs[sid].fbits[fi] > 0) {
            int bits = c->structs[sid].fbits[fi];
            int boff = c->structs[sid].fbitoff[fi];
            int mask = (bits >= 31) ? -1 : ((1 << bits) - 1);
            int clear = ~(mask << boff);
            if (!push(c, mask, n) || !byte(c, CL_OP_AND, n))
                return 0;
            if (boff && (!push(c, boff, n) || !byte(c, CL_OP_SHL, n)))
                return 0;
            if (gen_field_addr(c, n->value, fi, n->right, n) != 1 ||
                !byte(c, CL_OP_LOAD, n) || !push(c, clear, n) ||
                !byte(c, CL_OP_AND, n))
                return 0;
            if (!byte(c, CL_OP_SWAP, n) || !byte(c, CL_OP_OR, n))
                return 0;
            if (gen_field_addr(c, n->value, fi, n->right, n) != 1)
                return 0;
            return byte(c, CL_OP_STORE, n);
        }
        if (!isf && fw == 2) {
            if (!byte(c, CL_OP_DUP, n)) {
                return 0;
            }
            if (gen_field_addr(c, n->value, fi, n->right, n) != 1) {
                return 0;
            }
            if (!byte(c, CL_OP_STOREB, n) || !push(c, 8, n) ||
                !byte(c, CL_OP_SHR, n)) {
                return 0;
            }
            if (gen_field_addr(c, n->value, fi, n->right, n) != 1) {
                return 0;
            }
            if (!push(c, 1, n) || !byte(c, CL_OP_ADD, n) ||
                !byte(c, CL_OP_STOREB, n)) {
                return 0;
            }
            return 1;
        }
        if (gen_field_addr(c, n->value, fi, n->right, n) != 1) {
            return 0;
        }
        if (isf) {
            return byte(c, CL_OP_FSTORE, n);
        }
        if (fw >= 8) {
            return byte(c, CL_OP_STORE64, n);
        }
        if (fw == 1) {
            return byte(c, CL_OP_STOREB, n);
        }
        return byte(c, CL_OP_STORE, n);
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
        } else if (!is_main) {
            /* Void return still leaves a slot so CALLI callers can DROP. */
            if (!push(c, 0, n)) {
                return 0;
            }
        }
        return byte(c, is_main ? CL_OP_HALT : CL_OP_RET, n);
    }
    if (n->kind == N_BREAK) {
        int p;
        int sp;
        if (c->loop_sp == 0) {
            return fail(c, n->line, n->column, "break outside loop");
        }
        p = branch(c, CL_OP_JMP, n);
        if (p < 0) {
            return 0;
        }
        sp = c->loop_sp - 1;
        if (c->loop_nbrk[sp] == LOOP_PATCH_MAX) {
            return fail(c, n->line, n->column, "too many breaks");
        }
        c->loop_brk[sp][c->loop_nbrk[sp]++] = p;
        return 1;
    }
    if (n->kind == N_CONTINUE) {
        int p;
        int sp;
        if (c->loop_sp == 0) {
            return fail(c, n->line, n->column, "continue outside loop");
        }
        p = branch(c, CL_OP_JMP, n);
        if (p < 0) {
            return 0;
        }
        sp = c->loop_sp - 1;
        while (sp >= 0 && c->loop_switch[sp]) {
            sp--;
        }
        if (sp < 0) {
            return fail(c, n->line, n->column, "continue outside loop");
        }
        if (c->loop_ncont[sp] == LOOP_PATCH_MAX) {
            return fail(c, n->line, n->column, "too many continues");
        }
        c->loop_cont[sp][c->loop_ncont[sp]++] = p;
        return 1;
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
        int sp;
        if (!loop_enter(c, n, 0)) {
            return 0;
        }
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
        sp = c->loop_sp - 1;
        if (!loop_patch_list(c, c->loop_brk[sp], c->loop_nbrk[sp], c->pc, n) ||
            !loop_patch_list(c, c->loop_cont[sp], c->loop_ncont[sp], begin, n)) {
            return 0;
        }
        c->loop_sp--;
        return 1;
    }
    if (n->kind == N_FOR) {
        size_t begin;
        size_t cont;
        int sp;
        if (!loop_enter(c, n, 0)) {
            return 0;
        }
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
        cont = c->pc;
        if (!gen_stmt(c, n->third)) {
            return 0;
        }
        q = branch(c, CL_OP_JMP, n);
        if (q < 0 || !patch(c, q, begin, n) || !patch(c, p, c->pc, n)) {
            return 0;
        }
        sp = c->loop_sp - 1;
        if (!loop_patch_list(c, c->loop_brk[sp], c->loop_nbrk[sp], c->pc, n) ||
            !loop_patch_list(c, c->loop_cont[sp], c->loop_ncont[sp], cont, n)) {
            return 0;
        }
        c->loop_sp--;
        return 1;
    }
    if (n->kind == N_DO) {
        size_t begin = c->pc;
        int sp;
        if (!loop_enter(c, n, 0)) {
            return 0;
        }
        if (!gen_block(c, n->right)) {
            return 0;
        }
        v = gen_expr(c, n->left);
        if (v != 1) {
            return 0;
        }
        p = branch(c, CL_OP_JNZ, n);
        if (p < 0 || !patch(c, p, begin, n)) {
            return 0;
        }
        sp = c->loop_sp - 1;
        if (!loop_patch_list(c, c->loop_brk[sp], c->loop_nbrk[sp], c->pc, n) ||
            !loop_patch_list(c, c->loop_cont[sp], c->loop_ncont[sp], begin, n)) {
            return 0;
        }
        c->loop_sp--;
        return 1;
    }
    if (n->kind == N_SWITCH) {
        int s;
        int case_jmp[SWITCH_CASE_MAX];
        int case_id[SWITCH_CASE_MAX];
        int nc = 0;
        int has_def = 0;
        int to_def = -1;
        int to_end = -1;
        int i;
        if (!loop_enter(c, n, 1)) {
            return 0;
        }
        if (gen_expr(c, n->left) != 1) {
            return 0;
        }
        s = c->nodes[n->right].left;
        while (s >= 0) {
            if (c->nodes[s].kind == N_DEFAULT) {
                has_def = 1;
            }
            s = c->nodes[s].next;
        }
        s = c->nodes[n->right].left;
        while (s >= 0) {
            Node *st = &c->nodes[s];
            if (st->kind == N_CASE) {
                int jz;
                if (nc == SWITCH_CASE_MAX) {
                    return fail(c, n->line, n->column, "too many cases");
                }
                if (!byte(c, CL_OP_DUP, n) || !push(c, st->value, n) ||
                    !byte(c, CL_OP_EQ, n)) {
                    return 0;
                }
                jz = branch(c, CL_OP_JZ, n);
                if (jz < 0 || !byte(c, CL_OP_DROP, n)) {
                    return 0;
                }
                case_jmp[nc] = branch(c, CL_OP_JMP, n);
                if (case_jmp[nc] < 0) {
                    return 0;
                }
                case_id[nc] = s;
                nc++;
                if (!patch(c, jz, c->pc, n)) {
                    return 0;
                }
            }
            s = st->next;
        }
        if (!byte(c, CL_OP_DROP, n)) {
            return 0;
        }
        if (has_def) {
            to_def = branch(c, CL_OP_JMP, n);
            if (to_def < 0) {
                return 0;
            }
        } else {
            to_end = branch(c, CL_OP_JMP, n);
            if (to_end < 0) {
                return 0;
            }
        }
        s = c->nodes[n->right].left;
        while (s >= 0) {
            Node *st = &c->nodes[s];
            if (st->kind == N_CASE) {
                for (i = 0; i < nc; ++i) {
                    if (case_id[i] == s && !patch(c, case_jmp[i], c->pc, n)) {
                        return 0;
                    }
                }
            } else if (st->kind == N_DEFAULT) {
                if (to_def >= 0 && !patch(c, to_def, c->pc, n)) {
                    return 0;
                }
            } else if (!gen_stmt(c, s)) {
                return 0;
            }
            s = st->next;
        }
        if (to_end >= 0 && !patch(c, to_end, c->pc, n)) {
            return 0;
        }
        {
            int sp = c->loop_sp - 1;
            if (!loop_patch_list(c, c->loop_brk[sp], c->loop_nbrk[sp], c->pc, n)) {
                return 0;
            }
        }
        c->loop_sp--;
        return 1;
    }
    if (n->kind == N_CASE) {
        return 1;
    }
    if (n->kind == N_DEFAULT) {
        return 1;
    }
    if (n->kind == N_LABEL) {
        if (c->nlabels < LABEL_MAX) {
            text(c->label_name[c->nlabels], NAME_MAX, n->name);
            c->label_pc[c->nlabels] = (int)c->pc;
            c->nlabels++;
        }
        return 1;
    }
    if (n->kind == N_GOTO) {
        p = branch(c, CL_OP_JMP, n);
        if (p < 0) {
            return 0;
        }
        if (c->ngoto < LABEL_MAX) {
            c->goto_at[c->ngoto] = p;
            text(c->goto_name[c->ngoto], NAME_MAX, n->name);
            c->ngoto++;
        }
        return 1;
    }
    if (n->kind == N_ASM) {
        ClasmResult ar;
        uint8_t buf[2048];
        const char *src = c->str_pool + n->value;
        if (!clasm_compile(src, (size_t)slen_local(src), buf, sizeof(buf), &ar)) {
            return fail(c, n->line, n->column,
                        ar.diag.message[0] ? ar.diag.message : "asm error");
        }
        {
            size_t k;
            for (k = 0; k < ar.code_size; ++k) {
                if (!byte(c, buf[k], n)) {
                    return 0;
                }
            }
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

static int chrisc_emit(Compiler *c, ChrisResult *result) {
    int i;
    int main_i = -1;
    if (c->str_len > 0) {
        c->str_base = (c->mem_next + 3u) & ~3u;
        if (c->str_base + (uint32_t)c->str_len > CLVM_MEMORY_SIZE) {
            return fail(c, 1, 1, "string pool exceeds memory");
        }
        c->mem_next = c->str_base + (uint32_t)c->str_len;
    }
    c->icall_base = (c->mem_next + 7u) & ~7u;
    c->mem_next = c->icall_base + (uint32_t)FUNC_ARG_MAX * 8u;
    c->va_base = c->mem_next;
    c->mem_next += 32u * 8u;
    for (i = 0; i < c->nfuncs; ++i) {
        Node *body;
        uint8_t end_op;
        int a;
        maybe_yield();
        if (c->funcs[i].body < 0) {
            continue;
        }
        body = &c->nodes[c->funcs[i].body];
        c->cur_fn = i;
        c->funcs[i].entry = (int)c->pc;
        if (!byte(c, CL_OP_SAFEPOINT, body)) {
            return 0;
        }
        for (a = 0; a < c->funcs[i].argc; ++a) {
            if (!push(c, (int32_t)c->icall_base + a * 8, body) ||
                !byte(c, c->funcs[i].arg_float[a] ? CL_OP_FLOAD : CL_OP_LOAD64,
                      body) ||
                !push(c, (int32_t)c->funcs[i].arg_addr[a], body) ||
                !byte(c, c->funcs[i].arg_float[a] ? CL_OP_FSTORE :
                      (c->funcs[i].arg_width[a] >= 8 ? CL_OP_STORE64 : CL_OP_STORE),
                      body)) {
                return 0;
            }
        }
        if (!gen_block(c, c->funcs[i].body)) {
            return 0;
        }
        end_op = c->funcs[i].is_main ? CL_OP_HALT : CL_OP_RET;
        /* Always emit a fall-through epilogue. A trailing `if { return; }`
         * leaves RET as the last opcode even though the false path jumps
         * past it into the next function (editor V: handle_normal_char
         * fell into handle_text and recursed until csp=64). */
        if (!c->funcs[i].is_main && c->funcs[i].ret == 0) {
            if (!push(c, 0, body)) {
                return 0;
            }
        }
        if (!byte(c, end_op, body)) {
            return 0;
        }
        if (c->funcs[i].is_main) {
            main_i = i;
        }
    }
    for (i = 0; i < c->ngoto; ++i) {
        int li;
        Node dummy;
        dummy.line = 1;
        dummy.column = 1;
        for (li = 0; li < c->nlabels; ++li) {
            if (same(c->label_name[li], c->goto_name[i])) {
                if (!patch(c, c->goto_at[i], (size_t)c->label_pc[li], &dummy)) {
                    return 0;
                }
                break;
            }
        }
    }
    for (i = 0; i < c->npatches; ++i) {
        int fn = c->patches[i].fn;
        if (c->funcs[fn].entry < 0) {
            return fail(c, 1, 1, "unresolved function");
        }
        if (!patch(c, c->patches[i].at, (size_t)c->funcs[fn].entry,
                   &c->nodes[c->funcs[fn].body >= 0 ? c->funcs[fn].body : 0])) {
            return 0;
        }
    }
    for (i = 0; i < c->nfnptr; ++i) {
        int fn = c->fnptr_fn[i];
        int at = c->fnptr_at[i];
        uint32_t entry = (uint32_t)c->funcs[fn].entry;
        if (at < 0 || (size_t)at + 4 > c->pc) {
            return 0;
        }
        c->out[at] = (uint8_t)entry;
        c->out[at + 1] = (uint8_t)(entry >> 8);
        c->out[at + 2] = (uint8_t)(entry >> 16);
        c->out[at + 3] = (uint8_t)(entry >> 24);
    }
    if ((c->str_len > 0 || c->nginits > 0) && main_i >= 0) {
        int init_pc = (int)c->pc;
        Node *body = &c->nodes[c->funcs[main_i].body];
        int jmp;
        for (i = 0; i < c->str_len; ++i) {
            if (!push(c, (int32_t)(unsigned char)c->str_pool[i], body) ||
                !push(c, (int32_t)c->str_base + i, body) ||
                !byte(c, CL_OP_STOREB, body)) {
                return 0;
            }
        }
        for (i = 0; i < c->nginits; ++i) {
            uint8_t st;
            if (gen_expr(c, c->ginit_expr[i]) != 1) {
                return 0;
            }
            st = ginit_store_w(c->ginit_w[i]);
            if (!push(c, (int32_t)c->ginit_addr[i], body) ||
                !byte(c, st, body)) {
                return 0;
            }
        }
        jmp = branch(c, CL_OP_JMP, body);
        if (jmp < 0 ||
            !patch(c, jmp, (size_t)c->funcs[main_i].entry, body)) {
            return 0;
        }
        result->entry = (uint32_t)init_pc;
    } else if (main_i >= 0) {
        result->entry = (uint32_t)c->funcs[main_i].entry;
    }
    result->code_size = c->pc;
    result->variables = (unsigned)c->nsyms;
    result->nfiles = 0;
    for (i = 0; i < c->nfiles && result->nfiles < CHRIS_FILE_MAX; ++i) {
        text(result->file_path[result->nfiles], sizeof(result->file_path[0]),
             c->files[i]);
        result->nfiles++;
    }
    result->abi_major = c->abi_major ? c->abi_major : 1;
    result->abi_minor = c->abi_minor;
    result->nexports = 0;
    for (i = 0; i < c->nfuncs && result->nexports < 64; ++i) {
        int k = 0;
        if (c->funcs[i].body < 0 || c->funcs[i].entry < 0) {
            continue;
        }
        while (c->funcs[i].name[k] && k < 31) {
            result->export_name[result->nexports][k] = c->funcs[i].name[k];
            k++;
        }
        result->export_name[result->nexports][k] = 0;
        result->export_pc[result->nexports] = (uint32_t)c->funcs[i].entry;
        result->export_argc[result->nexports] = c->funcs[i].argc;
        result->nexports++;
    }
    return 1;
}

int chrisc_compile_ex(const char *path, const char *source, size_t source_size,
                      ChriscReadFn read, void *user, uint8_t *code,
                      size_t code_cap, ChrisResult *result) {
    Compiler *c = &g_chrisc;
    int root;
    int i;
    int main_i = -1;
    const char *lex_src;
    size_t lex_n;

    if (!source || !code || !result) {
        return 0;
    }
    c->ntok = c->pos = c->nnode = c->nargs = c->nsyms = 0;
    c->nstructs = c->nfuncs = c->npatches = c->nfnptr = c->cur_fn = 0;
    c->scope_base = 0;
    c->scope_sp = 0;
    c->scope_seq = 0;
    c->mem_next = 0;
    c->out = code;
    c->cap = code_cap;
    c->pc = 0;
    c->result = result;
    c->read_fn = read;
    c->read_user = user;
    c->nfiles = 0;
    c->nmap = 0;
    c->str_len = 0;
    c->str_base = 0;
    c->loop_sp = 0;
    c->pp_skip = 0;
    c->pp_depth = 0;
    c->ndef = 0;
    c->n_tls = 0;
    lookups_reset(c);
    inc_cache_reset();
    add_builtin_macros(c);
    c->nlabels = 0;
    c->ngoto = 0;
    c->ntypedef = 0;
    c->nconst = 0;
    c->pragma_once_n = 0;
    c->nginits = 0;
    c->va_base = 0;
    c->icall_base = 0;
    c->tu_id = 0;
    c->saw_static = 0;
    c->saw_const = 0;
    c->align_next = 0;
    c->field_bits = 0;
    c->pack_pragma = 0;
    c->abi_major = 1;
    c->abi_minor = 0;
    result->code_size = 0;
    result->entry = 0;
    result->variables = 0;
    result->map_n = 0;
    clear_diag(result);
    {
        size_t ulen = 0;
        int unit_line = 1;
        char *src = g_tu;
        size_t nn = source_size;
        if (source != g_tu) {
            size_t k = 0;
            if (nn >= CHRIS_SOURCE_MAX) {
                nn = CHRIS_SOURCE_MAX - 1;
            }
            while (k < nn) {
                g_tu[k] = source[k];
                k++;
            }
            g_tu[nn] = 0;
        }
        nn = compact_line_cont(src, nn);
        if (!expand_file(c, path ? path : "", src, nn, &ulen,
                         &unit_line, 0)) {
            return 0;
        }
        if (ulen == 0) {
            return fail(c, 1, 1, "source size outside limit");
        }
        lex_src = g_unit;
        lex_n = ulen;
    }
    if (!lex(c, lex_src, lex_n)) {
        return 0;
    }
    root = program(c);
    if (root < 0) {
        return 0;
    }
    (void)root;
    (void)i;
    (void)main_i;
    return chrisc_emit(c, result);
}

int chrisc_compile(const char *source, size_t source_size, uint8_t *code,
                   size_t code_cap, ChrisResult *result) {
    return chrisc_compile_ex(0, source, source_size, 0, 0, code, code_cap,
                             result);
}

int chrisc_compile_files_ex(const char **paths, int npaths, ChriscReadFn read,
                            void *user, uint8_t *code, size_t code_cap,
                            ChrisResult *result, ChriscProgressFn progress,
                            void *progress_user) {
    Compiler *c = &g_chrisc;
    int i;
    if (!paths || npaths < 1 || !code || !result) {
        return 0;
    }
    if (npaths == 1) {
        int n;
        if (!read) {
            return 0;
        }
        if (progress)
            progress(progress_user, 0, 1, paths[0]);
        n = read(user, paths[0], g_tu, (int)CHRIS_SOURCE_MAX - 1);
        if (n < 0) {
            result->diag_n = 0;
            set_diag(result, 1, 1, paths[0] ? paths[0] : "",
                     "cannot read source file");
            return 0;
        }
        g_tu[n] = 0;
        return chrisc_compile_ex(paths[0], g_tu, (size_t)n, read,
                                 user, code, code_cap, result);
    }
    c->ntok = c->pos = c->nnode = c->nargs = c->nsyms = 0;
    c->nstructs = c->nfuncs = c->npatches = c->nfnptr = c->cur_fn = 0;
    c->scope_base = 0;
    c->scope_sp = 0;
    c->scope_seq = 0;
    c->mem_next = 0;
    c->out = code;
    c->cap = code_cap;
    c->pc = 0;
    c->result = result;
    c->read_fn = read;
    c->read_user = user;
    c->nfiles = 0;
    c->nmap = 0;
    c->str_len = 0;
    c->str_base = 0;
    c->loop_sp = 0;
    c->pp_skip = 0;
    c->pp_depth = 0;
    c->ndef = 0;
    c->n_tls = 0;
    lookups_reset(c);
    inc_cache_reset();
    add_builtin_macros(c);
    c->nlabels = 0;
    c->ngoto = 0;
    c->ntypedef = 0;
    c->nconst = 0;
    c->pragma_once_n = 0;
    c->nginits = 0;
    c->va_base = 0;
    c->icall_base = 0;
    c->tu_id = 0;
    c->saw_static = 0;
    c->saw_const = 0;
    c->align_next = 0;
    c->field_bits = 0;
    c->pack_pragma = 0;
    c->abi_major = 1;
    c->abi_minor = 0;
    result->code_size = 0;
    result->entry = 0;
    result->variables = 0;
    result->map_n = 0;
    clear_diag(result);
    for (i = 0; i < npaths; ++i) {
        int n;
        size_t ulen = 0;
        int unit_line = 1;
        if (!read) {
            return 0;
        }
        if (progress)
            progress(progress_user, i, npaths, paths[i]);
        n = read(user, paths[i], g_tu, (int)CHRIS_SOURCE_MAX - 1);
        if (n < 0) {
            set_diag(result, 1, 1, paths[i] ? paths[i] : "",
                     "cannot read source file");
            return 0;
        }
        g_tu[n] = 0;
        n = (int)compact_line_cont(g_tu, (size_t)n);
        c->tu_id = i + 1;
        c->ntok = c->pos = 0;
        c->pp_skip = 0;
        c->pp_depth = 0;
        if (!expand_file(c, paths[i], g_tu, (size_t)n, &ulen,
                         &unit_line, 0)) {
            return 0;
        }
        if (ulen == 0) {
            continue;
        }
        if (!lex(c, g_unit, ulen)) {
            return 0;
        }
        if (parse_decls(c) != 1) {
            return 0;
        }
        if (progress)
            progress(progress_user, -(i + 1), npaths, paths[i]);
    }
    {
        int main_i = -1;
        for (i = 0; i < c->nfuncs; ++i) {
            if (c->funcs[i].is_main) {
                main_i = i;
                break;
            }
        }
        if (main_i < 0) {
            return fail(c, 1, 1, "expected main");
        }
    }
    if (progress)
        progress(progress_user, -(npaths + 1), npaths, "emit");
    maybe_yield();
    return chrisc_emit(c, result);
}

int chrisc_compile_files(const char **paths, int npaths, ChriscReadFn read,
                         void *user, uint8_t *code, size_t code_cap,
                         ChrisResult *result) {
    return chrisc_compile_files_ex(paths, npaths, read, user, code, code_cap,
                                   result, 0, 0);
}
