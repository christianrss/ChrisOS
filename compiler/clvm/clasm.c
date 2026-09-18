#include "clasm.h"
#include "clvm.h"

typedef enum ArgKind { ARG_NONE, ARG_I32, ARG_LABEL } ArgKind;
typedef struct OpInfo { const char *name; uint8_t op, size; ArgKind arg; } OpInfo;
typedef struct Label { char name[CLASM_NAME_MAX]; uint16_t address; } Label;
typedef struct Context {
    const char *source;
    size_t size;
    Label labels[CLASM_MAX_LABELS];
    int label_count;
    size_t pc;
    uint8_t *out;
    size_t out_cap;
    ClasmResult *result;
} Context;

static const OpInfo ops[] = {
    {"NOP",CL_OP_NOP,1,ARG_NONE},{"PUSH",CL_OP_PUSH,5,ARG_I32},
    {"ADD",CL_OP_ADD,1,ARG_NONE},{"SUB",CL_OP_SUB,1,ARG_NONE},
    {"MUL",CL_OP_MUL,1,ARG_NONE},{"DIV",CL_OP_DIV,1,ARG_NONE},
    {"DUP",CL_OP_DUP,1,ARG_NONE},{"PRINT",CL_OP_PRINT,1,ARG_NONE},
    {"HALT",CL_OP_HALT,1,ARG_NONE},{"JMP",CL_OP_JMP,3,ARG_LABEL},
    {"JZ",CL_OP_JZ,3,ARG_LABEL},{"CALL",CL_OP_CALL,3,ARG_LABEL},
    {"RET",CL_OP_RET,1,ARG_NONE},{"LOAD",CL_OP_LOAD,1,ARG_NONE},
    {"STORE",CL_OP_STORE,1,ARG_NONE},{"DROP",CL_OP_DROP,1,ARG_NONE},
    {"SWAP",CL_OP_SWAP,1,ARG_NONE},{"EQ",CL_OP_EQ,1,ARG_NONE},
    {"LT",CL_OP_LT,1,ARG_NONE},{"JNZ",CL_OP_JNZ,3,ARG_LABEL},
    {"MOD",CL_OP_MOD,1,ARG_NONE},{"NE",CL_OP_NE,1,ARG_NONE},
    {"LE",CL_OP_LE,1,ARG_NONE},{"GT",CL_OP_GT,1,ARG_NONE},
    {"GE",CL_OP_GE,1,ARG_NONE},{"NEG",CL_OP_NEG,1,ARG_NONE},
    {"SYS",CL_OP_SYS,1,ARG_NONE}
};

static int upper(int c) { return c >= 'a' && c <= 'z' ? c - 32 : c; }
static int space(int c) { return c == ' ' || c == '\t' || c == '\r'; }
static int digit(int c) { return c >= '0' && c <= '9'; }
static int hex(int c) {
    if (digit(c)) return c - '0';
    c = upper(c);
    return c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
}

static void copy_text(char *dst, size_t cap, const char *src) {
    size_t i = 0;
    if (cap == 0) return;
    while (src[i] && i + 1 < cap) { dst[i] = src[i]; ++i; }
    dst[i] = 0;
}

static int error(Context *c, int line, int column, const char *message) {
    c->result->diag.line = line;
    c->result->diag.column = column;
    copy_text(c->result->diag.message, sizeof(c->result->diag.message), message);
    return 0;
}

static int same(const char *a, const char *b) {
    size_t i = 0;
    while (a[i] && b[i] && upper(a[i]) == upper(b[i])) ++i;
    return a[i] == 0 && b[i] == 0;
}

static int name_char(int c, int first) {
    return (c == '_') || (c >= 'A' && c <= 'Z') ||
           (c >= 'a' && c <= 'z') || (!first && digit(c));
}

static int word(const char *line, size_t n, size_t *at,
                char *out, size_t cap, int *column) {
    size_t k = 0;
    while (*at < n && space((unsigned char)line[*at])) ++*at;
    *column = (int)*at + 1;
    if (*at >= n || !name_char((unsigned char)line[*at], 1)) return 0;
    while (*at < n && name_char((unsigned char)line[*at], k == 0)) {
        if (k + 1 < cap) out[k++] = line[*at];
        ++*at;
    }
    out[k] = 0;
    return 1;
}

static int tail_empty(const char *line, size_t n, size_t at) {
    while (at < n && space((unsigned char)line[at])) ++at;
    return at == n || line[at] == ';' || line[at] == '#';
}

static const OpInfo *find_op(const char *name) {
    size_t i;
    for (i = 0; i < sizeof(ops) / sizeof(ops[0]); ++i)
        if (same(name, ops[i].name)) return &ops[i];
    return 0;
}

static int find_label(Context *c, const char *name) {
    int i;
    for (i = 0; i < c->label_count; ++i)
        if (same(c->labels[i].name, name)) return i;
    return -1;
}

static int add_label(Context *c, const char *name, int line, int column) {
    size_t i = 0;
    if (find_label(c, name) >= 0) return error(c, line, column, "duplicate label");
    if (c->label_count == CLASM_MAX_LABELS)
        return error(c, line, column, "too many labels");
    while (name[i]) ++i;
    if (i >= CLASM_NAME_MAX) return error(c, line, column, "label too long");
    copy_text(c->labels[c->label_count].name, CLASM_NAME_MAX, name);
    c->labels[c->label_count].address = (uint16_t)c->pc;
    ++c->label_count;
    return 1;
}

static int number(const char *line, size_t n, size_t *at, int32_t *value) {
    uint32_t v = 0;
    int negative = 0, base = 10, d, any = 0;
    while (*at < n && space((unsigned char)line[*at])) ++*at;
    if (*at < n && line[*at] == '-') { negative = 1; ++*at; }
    if (*at + 1 < n && line[*at] == '0' &&
        (line[*at + 1] == 'x' || line[*at + 1] == 'X')) {
        base = 16; *at += 2;
    }
    while (*at < n && (d = hex((unsigned char)line[*at])) >= 0 && d < base) {
        if (v > (0xffffffffu - (uint32_t)d) / (uint32_t)base) return 0;
        v = v * (uint32_t)base + (uint32_t)d;
        ++*at; any = 1;
    }
    if (!any) return 0;
    if (negative) v = 0u - v;
    *value = (int32_t)v;
    return 1;
}

static void emit32(uint8_t *p, uint32_t v) {
    p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8);
    p[2]=(uint8_t)(v>>16); p[3]=(uint8_t)(v>>24);
}

static int parse_line(Context *c, const char *line, size_t n,
                      int line_no, int pass) {
    char first[CLASM_NAME_MAX], arg[CLASM_NAME_MAX];
    size_t at = 0, save;
    int column = 1, arg_col;
    const OpInfo *op;
    int32_t immediate;
    if (tail_empty(line, n, 0)) return 1;
    if (!word(line, n, &at, first, sizeof(first), &column))
        return error(c, line_no, column, "expected instruction or label");
    save = at;
    while (at < n && space((unsigned char)line[at])) ++at;
    if (at < n && line[at] == ':') {
        ++at;
        if (pass == 1 && !add_label(c, first, line_no, column)) return 0;
        if (tail_empty(line, n, at)) return 1;
        if (!word(line, n, &at, first, sizeof(first), &column))
            return error(c, line_no, (int)at + 1, "expected instruction");
    } else {
        at = save;
    }
    op = find_op(first);
    if (!op) return error(c, line_no, column, "unknown instruction");
    if (c->pc + op->size > CLVM_MAX_CODE || c->pc + op->size > c->out_cap)
        return error(c, line_no, column, "bytecode output full");
    if (pass == 2) c->out[c->pc] = op->op;
    if (op->arg == ARG_I32) {
        arg_col = (int)at + 1;
        if (!number(line, n, &at, &immediate))
            return error(c, line_no, arg_col, "expected i32");
        if (!tail_empty(line, n, at))
            return error(c, line_no, (int)at + 1, "extra text after operand");
        if (pass == 2) emit32(c->out + c->pc + 1, (uint32_t)immediate);
    } else if (op->arg == ARG_LABEL) {
        int index;
        int32_t rel;
        if (!word(line, n, &at, arg, sizeof(arg), &arg_col))
            return error(c, line_no, (int)at + 1, "expected label");
        if (!tail_empty(line, n, at))
            return error(c, line_no, (int)at + 1, "extra text after label");
        if (pass == 2) {
            index = find_label(c, arg);
            if (index < 0) return error(c, line_no, arg_col, "undefined label");
            rel = (int32_t)c->labels[index].address - (int32_t)(c->pc + 3);
            if (rel < -32768 || rel > 32767)
                return error(c, line_no, arg_col, "relative jump too far");
            c->out[c->pc + 1] = (uint8_t)rel;
            c->out[c->pc + 2] = (uint8_t)((uint32_t)rel >> 8);
        }
    } else if (!tail_empty(line, n, at)) {
        return error(c, line_no, (int)at + 1, "instruction takes no operand");
    }
    c->pc += op->size;
    return 1;
}

static int run_pass(Context *c, int pass) {
    size_t begin = 0, end;
    int line = 1;
    c->pc = 0;
    while (begin < c->size) {
        end = begin;
        while (end < c->size && c->source[end] != '\n') ++end;
        if (!parse_line(c, c->source + begin, end - begin, line, pass)) return 0;
        begin = end < c->size ? end + 1 : end;
        ++line;
    }
    return 1;
}

int clasm_compile(const char *source, size_t source_size,
                  uint8_t *code, size_t code_cap, ClasmResult *result) {
    Context c;
    if (!source || !code || !result) return 0;
    result->code_size = 0; result->entry = 0;
    result->diag.line = 0; result->diag.column = 0; result->diag.message[0] = 0;
    c.source=source; c.size=source_size; c.label_count=0;
    c.out=code; c.out_cap=code_cap; c.result=result; c.pc=0;
    if (source_size == 0 || source_size > CLASM_MAX_SOURCE)
        return error(&c, 1, 1, "source size outside limit");
    if (!run_pass(&c, 1) || c.pc == 0) return 0;
    if (!run_pass(&c, 2)) return 0;
    result->code_size = c.pc;
    {
        int entry = find_label(&c, "main");
        result->entry = entry >= 0 ? c.labels[entry].address : 0;
    }
    return 1;
}
