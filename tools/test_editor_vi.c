#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "chrisc.h"
#include "clvm.h"
#include "clvm_vm.h"
#include "jit.h"
#include "jit_compile.h"

static int sys_nop(ClvmVm *vm, int32_t id, void *user) {
    (void)vm;
    (void)id;
    (void)user;
    return -1;
}

static int read_file(void *user, const char *path, char *out, int cap) {
    FILE *f;
    size_t n;
    (void)user;
    f = fopen(path, "rb");
    if (!f)
        return -1;
    n = fread(out, 1, (size_t)cap - 1u, f);
    fclose(f);
    out[n] = 0;
    return (int)n;
}

static int insn_len(const uint8_t *code, uint32_t pc, uint32_t size) {
    uint8_t op;
    if (pc >= size)
        return 0;
    op = code[pc];
    switch (op) {
    case CL_OP_PUSH:
    case CL_OP_FPUSH:
        return 5;
    case CL_OP_JMP:
    case CL_OP_JZ:
    case CL_OP_JNZ:
    case CL_OP_CALL:
        return 3;
    case CL_OP_LDARG:
    case CL_OP_STLOC:
    case CL_OP_LDLOC:
        return 2;
    case CL_OP_NEWOBJ:
    case CL_OP_LDFLD:
    case CL_OP_STFLD:
    case CL_OP_CALLT:
    case CL_OP_LDSTR:
    case CL_OP_JMP32:
    case CL_OP_JZ32:
    case CL_OP_JNZ32:
    case CL_OP_CALL32:
        return 5;
    case CL_OP_PUSH64:
        return 9;
    default:
        return 1;
    }
}

static int32_t rd_i32(const uint8_t *c, uint32_t pc) {
    return (int32_t)((uint32_t)c[pc] | ((uint32_t)c[pc + 1u] << 8) |
                     ((uint32_t)c[pc + 2u] << 16) | ((uint32_t)c[pc + 3u] << 24));
}

static int16_t rd_i16(const uint8_t *c, uint32_t pc) {
    return (int16_t)((uint16_t)c[pc] | ((uint16_t)c[pc + 1u] << 8));
}

static int compile_src(const char *src, uint8_t *code, size_t cap,
                       ChrisResult *res) {
    memset(res, 0, sizeof(*res));
    if (!chrisc_compile(src, strlen(src), code, cap, res)) {
        fprintf(stderr, "compile: %s:%d:%d %s\n", res->diag.file, res->diag.line,
                res->diag.column, res->diag.message);
        return 0;
    }
    return 1;
}

static int run_interp(const uint8_t *code, const ChrisResult *res, ClvmVm *vm,
                      uint32_t budget) {
    ClvmImage img;
    memset(&img, 0, sizeof(img));
    img.code = (uint8_t *)code;
    img.code_size = (uint32_t)res->code_size;
    img.entry = res->entry;
    clvm_vm_init(vm, &img, sys_nop, 0);
    return (int)clvm_step(vm, budget);
}

static int test_fallthrough(void) {
    static const char src[] =
        "int g;\n"
        "void f(int x) {\n"
        "  if (x) {\n"
        "    g = 5;\n"
        "    return;\n"
        "  }\n"
        "}\n"
        "void poison() {\n"
        "  g = 99;\n"
        "}\n"
        "void main() {\n"
        "  g = 7;\n"
        "  f(0);\n"
        "  if (g == 99) {\n"
        "    while (1) {\n"
        "    }\n"
        "  }\n"
        "}\n";
    static uint8_t code[65536];
    ChrisResult res;
    ClvmVm vm;
    ClvmStepResult st;
    if (!compile_src(src, code, sizeof(code), &res))
        return 0;
    st = (ClvmStepResult)run_interp(code, &res, &vm, 100000u);
    if (st != CLVM_STEP_HALT) {
        fprintf(stderr, "fallthrough: expected HALT got %d fault=%d\n", (int)st,
                (int)vm.fault);
        return 0;
    }
    puts("fallthrough: ok");
    return 1;
}

static int test_call_overflow_interp(void) {
    static const char src[] =
        "void f() {\n"
        "  f();\n"
        "}\n"
        "void main() {\n"
        "  f();\n"
        "}\n";
    static uint8_t code[65536];
    ChrisResult res;
    ClvmVm vm;
    ClvmStepResult st;
    if (!compile_src(src, code, sizeof(code), &res))
        return 0;
    st = (ClvmStepResult)run_interp(code, &res, &vm, 100000u);
    if (st != CLVM_STEP_FAULT || vm.fault != CLVM_FAULT_CALL_OVERFLOW) {
        fprintf(stderr, "overflow interp: st=%d fault=%d csp=%u\n", (int)st,
                (int)vm.fault, (unsigned)vm.csp);
        return 0;
    }
    puts("call overflow interp: ok");
    return 1;
}

static int test_call_overflow_jit(void) {
    static const char src[] =
        "void f() {\n"
        "  f();\n"
        "}\n"
        "void main() {\n"
        "  f();\n"
        "}\n";
    static uint8_t code[65536];
    ChrisResult res;
    ClvmImage img;
    ClvmVm vm;
    JitBuf buf;
    JitFn fn;
    ClvmStepResult st;
    if (!compile_src(src, code, sizeof(code), &res))
        return 0;
    memset(&img, 0, sizeof(img));
    img.code = code;
    img.code_size = (uint32_t)res.code_size;
    img.entry = res.entry;
    memset(&buf, 0, sizeof(buf));
    if (jit_compile_image(&img, &buf, &fn) != 0) {
        fprintf(stderr, "overflow jit: compile failed\n");
        return 0;
    }
    clvm_vm_init(&vm, &img, sys_nop, 0);
    st = fn(&vm, 100000u, 0);
    jit_free(&buf);
    if (st != CLVM_STEP_FAULT || vm.fault != CLVM_FAULT_CALL_OVERFLOW) {
        fprintf(stderr, "overflow jit: st=%d fault=%d csp=%u\n", (int)st,
                (int)vm.fault, (unsigned)vm.csp);
        return 0;
    }
    puts("call overflow jit: ok");
    return 1;
}

static int test_yank_ops(void) {
    static const char src[] =
        "char buf[16];\n"
        "int len;\n"
        "char yk[8];\n"
        "int yn;\n"
        "void ycopy(int lo, int hi) {\n"
        "  int i;\n"
        "  yn = hi - lo;\n"
        "  i = 0;\n"
        "  while (i < yn) {\n"
        "    storeb(yk + i, loadb(buf + lo + i));\n"
        "    i = i + 1;\n"
        "  }\n"
        "}\n"
        "void delr(int lo, int hi) {\n"
        "  int n;\n"
        "  int i;\n"
        "  n = hi - lo;\n"
        "  i = lo;\n"
        "  while (i + n <= len) {\n"
        "    storeb(buf + i, loadb(buf + i + n));\n"
        "    i = i + 1;\n"
        "  }\n"
        "  len = len - n;\n"
        "}\n"
        "void putat(int at) {\n"
        "  int i;\n"
        "  i = len;\n"
        "  while (i > at) {\n"
        "    i = i - 1;\n"
        "    storeb(buf + i + yn, loadb(buf + i));\n"
        "  }\n"
        "  i = 0;\n"
        "  while (i < yn) {\n"
        "    storeb(buf + at + i, loadb(yk + i));\n"
        "    i = i + 1;\n"
        "  }\n"
        "  len = len + yn;\n"
        "}\n"
        "void main() {\n"
        "  storeb(buf, 97);\n"
        "  storeb(buf + 1, 98);\n"
        "  storeb(buf + 2, 99);\n"
        "  len = 3;\n"
        "  ycopy(0, 2);\n"
        "  delr(0, 2);\n"
        "  putat(1);\n"
        "  if (loadb(buf) != 99) {\n"
        "    while (1) {\n"
        "    }\n"
        "  }\n"
        "  if (loadb(buf + 1) != 97) {\n"
        "    while (1) {\n"
        "    }\n"
        "  }\n"
        "  if (loadb(buf + 2) != 98) {\n"
        "    while (1) {\n"
        "    }\n"
        "  }\n"
        "}\n";
    static uint8_t code[65536];
    ChrisResult res;
    ClvmVm vm;
    ClvmStepResult st;
    if (!compile_src(src, code, sizeof(code), &res))
        return 0;
    st = (ClvmStepResult)run_interp(code, &res, &vm, 100000u);
    if (st != CLVM_STEP_HALT) {
        fprintf(stderr, "yank: expected HALT got %d fault=%d\n", (int)st,
                (int)vm.fault);
        return 0;
    }
    puts("yank ops: ok");
    return 1;
}

static int test_editor_no_self_call(void) {
    static char lst[8192];
    static char paths[32][128];
    const char *pp[32];
    static uint8_t code[524288];
    ChrisResult res;
    FILE *f;
    size_t n;
    int npaths = 0;
    int p;
    uint32_t pc;
    int self = 0;

    f = fopen("APPS/EDITOR/EDITOR.LST", "rb");
    if (!f) {
        fprintf(stderr, "missing EDITOR.LST\n");
        return 0;
    }
    n = fread(lst, 1, sizeof(lst) - 1, f);
    fclose(f);
    lst[n] = 0;
    p = 0;
    while (p < (int)n && npaths < 32) {
        int j = 0;
        while (p < (int)n && (lst[p] == ' ' || lst[p] == '\t' || lst[p] == '\r' ||
                              lst[p] == '\n'))
            p++;
        if (p >= (int)n)
            break;
        if (lst[p] == '#') {
            while (p < (int)n && lst[p] != '\n')
                p++;
            continue;
        }
        while (p < (int)n && lst[p] != '\n' && lst[p] != '\r' && j < 127)
            paths[npaths][j++] = lst[p++];
        while (j > 0 && (paths[npaths][j - 1] == ' ' ||
                         paths[npaths][j - 1] == '\t'))
            j--;
        paths[npaths][j] = 0;
        if (j > 0) {
            pp[npaths] = paths[npaths];
            npaths++;
        }
        if (p < (int)n && (lst[p] == '\n' || lst[p] == '\r'))
            p++;
    }
    memset(&res, 0, sizeof(res));
    if (!chrisc_compile_files(pp, npaths, read_file, 0, code, sizeof(code),
                              &res)) {
        fprintf(stderr, "EDITOR: %s:%d:%d %s\n", res.diag.file, res.diag.line,
                res.diag.column, res.diag.message);
        return 0;
    }
    pc = 0;
    while (pc < (uint32_t)res.code_size) {
        int len = insn_len(code, pc, (uint32_t)res.code_size);
        uint8_t op;
        uint32_t tgt;
        int32_t rel;
        uint32_t next;
        if (len < 1)
            break;
        op = code[pc];
        if (op == CL_OP_CALL || op == CL_OP_CALL32) {
            next = pc + (uint32_t)len;
            if (op == CL_OP_CALL)
                rel = (int32_t)rd_i16(code, pc + 1u);
            else
                rel = rd_i32(code, pc + 1u);
            tgt = next + (uint32_t)rel;
            if (tgt == pc || rel == -5 || rel == -3) {
                fprintf(stderr, "EDITOR self CALL pc=%u rel=%d\n", (unsigned)pc,
                        (int)rel);
                self++;
            }
        }
        pc += (uint32_t)len;
    }
    if (self) {
        fprintf(stderr, "EDITOR self_calls=%d\n", self);
        return 0;
    }
    printf("EDITOR: %u bytes no self-call\n", (unsigned)res.code_size);
    return 1;
}

int main(void) {
    if (!test_fallthrough())
        return 1;
    if (!test_call_overflow_interp())
        return 1;
    if (!test_call_overflow_jit())
        return 1;
    if (!test_yank_ops())
        return 1;
    if (!test_editor_no_self_call())
        return 1;
    puts("test_editor_vi: ok");
    return 0;
}
