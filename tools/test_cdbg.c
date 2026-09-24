#include "chrisc/chrisc.h"
#include "debug/cdbg.h"

#include <stdio.h>
#include <string.h>

static int fail(const char *msg) {
    fprintf(stderr, "test_cdbg: %s\n", msg);
    return 1;
}

static int round_trip(void) {
    CdbgImage img;
    CdbgImage out;
    CdbgLine line;
    CdbgFunc fn;
    char path[96];
    uint8_t buf[4096];
    int n;
    uint32_t pc;
    uint16_t ln;
    uint16_t file;

    memset(&img, 0, sizeof(img));
    img.abi_major = 1;
    img.abi_minor = 2;
    img.exec_hash = 0x11223344u;
    img.source_hash = 0xabcdefu;
    img.nfiles = 2;
    img.files[0].id = 0;
    memcpy(img.files[0].path, "SRC/MAIN.CC", 12);
    img.files[1].id = 1;
    memcpy(img.files[1].path, "LIB/SIM.CC", 11);
    img.nlines = 3;
    img.lines[0].pc_start = 0;
    img.lines[0].pc_end = 10;
    img.lines[0].file_id = 0;
    img.lines[0].line = 1;
    img.lines[0].column = 1;
    img.lines[1].pc_start = 10;
    img.lines[1].pc_end = 40;
    img.lines[1].file_id = 1;
    img.lines[1].line = 4;
    img.lines[1].column = 3;
    img.lines[2].pc_start = 40;
    img.lines[2].pc_end = 80;
    img.lines[2].file_id = 0;
    img.lines[2].line = 8;
    img.lines[2].column = 1;
    img.nfuncs = 2;
    memcpy(img.funcs[0].name, "main", 5);
    img.funcs[0].start = 0;
    img.funcs[0].end = 40;
    img.funcs[0].file_id = 0;
    img.funcs[0].line = 1;
    img.funcs[0].argc = 0;
    memcpy(img.funcs[1].name, "sim", 4);
    img.funcs[1].start = 40;
    img.funcs[1].end = 80;
    img.funcs[1].file_id = 1;
    img.funcs[1].line = 4;
    img.funcs[1].argc = 1;

    n = cdbg_encode(buf, (int)sizeof(buf), &img);
    if (n < 28) {
        return fail("encode");
    }
    if (cdbg_decode(buf, n, &out) != n) {
        return fail("decode length");
    }
    if (out.nfiles != 2 || out.nlines != 3 || out.nfuncs != 2 ||
        out.exec_hash != img.exec_hash || strcmp(out.files[1].path, "LIB/SIM.CC") != 0 ||
        out.lines[1].file_id != 1 || out.lines[1].line != 4 ||
        out.funcs[1].argc != 1) {
        return fail("round trip fields");
    }
    if (!cdbg_line_at(&out, 12, &line) || line.file_id != 1 || line.line != 4) {
        return fail("line at pc");
    }
    if (!cdbg_func_at(&out, 12, &fn) || strcmp(fn.name, "main") != 0) {
        return fail("func at pc");
    }
    if (!cdbg_func_at(&out, 40, &fn) || strcmp(fn.name, "sim") != 0) {
        return fail("func boundary");
    }
    if (!cdbg_file_at(&out, 12, path, (int)sizeof(path)) ||
        strcmp(path, "LIB/SIM.CC") != 0) {
        return fail("file at pc");
    }
    if (!cdbg_parse_map_line("0x2A 9\n", &pc, &ln, &file) || pc != 0x2a ||
        ln != 9 || file != 0) {
        return fail("old map line");
    }
    if (!cdbg_parse_map_line("0x2A 9 3\n", &pc, &ln, &file) || file != 3 ||
        ln != 9) {
        return fail("map file id");
    }
    if (cdbg_parse_map_line("F main 16\n", &pc, &ln, &file)) {
        return fail("function line is not a pc map");
    }
    return 0;
}

static int read_inc(void *user, const char *path, char *out, int cap) {
    const char *src = "int added(int x) {\n return x + 1;\n}\n";
    int n = 0;
    (void)user;
    (void)path;
    while (src[n] && n + 1 < cap) {
        out[n] = src[n];
        ++n;
    }
    out[n] = 0;
    return n;
}

static int compile_file_ids(void) {
    static ChrisResult result;
    static uint8_t code[1 << 20];
    static uint8_t blob[1 << 16];
    const char *src =
        "#include \"inc.cc\"\n"
        "int main() {\n"
        " return added(3);\n"
        "}\n";
    int i;
    int saw_inc = 0;
    int n;
    CdbgImage img;
    CdbgLine line;

    memset(&result, 0, sizeof(result));
    if (!chrisc_compile_ex("SRC/MAIN.CC", src, strlen(src), read_inc, 0, code,
                           sizeof(code), &result)) {
        fprintf(stderr, "compile: %s\n", result.diag.message);
        return fail("compile include");
    }
    if (result.nfiles < 2) {
        return fail("file table");
    }
    if (strcmp(result.file_path[1], "SRC/inc.cc") != 0 &&
        strcmp(result.file_path[1], "inc.cc") != 0) {
        fprintf(stderr, "files %s | %s\n", result.file_path[0], result.file_path[1]);
        return fail("include path");
    }
    for (i = 0; i < result.map_n; ++i) {
        if (result.map[i].file_id != 0) {
            saw_inc = 1;
        }
    }
    if (!saw_inc) {
        return fail("map file_id still zero");
    }
    n = cdbg_from_result(blob, (int)sizeof(blob), &result, code,
                         (uint32_t)result.code_size, 0);
    if (n < 28 || cdbg_decode(blob, n, &img) < 0) {
        return fail("cdbg from result");
    }
    if (!cdbg_line_at(&img, img.lines[0].pc_start, &line)) {
        return fail("decoded line");
    }
    if (result.diag_n != 0) {
        return fail("success published a diagnostic");
    }
    memset(&result, 0, sizeof(result));
    if (chrisc_compile("int\n", 4, code, sizeof(code), &result)) {
        return fail("bad source compiled");
    }
    if (result.diag_n < 1 || result.diags[0].severity != CHRIS_SEV_ERROR ||
        result.diags[0].line != result.diag.line) {
        return fail("diagnostic array");
    }
    result.diag_n = 0;
    if (!chrisc_diag_push(&result, CHRIS_SEV_WARNING, 7, "A.CC", 2, 3, 2, 8,
                          "wide") ||
        !chrisc_diag_push(&result, CHRIS_SEV_NOTE, 8, "B.CC", 4, 1, 4, 1, "note")) {
        return fail("push");
    }
    if (result.diag_n != 2 || result.diags[1].code != 8) {
        return fail("push order");
    }
    result.diag_n = CHRIS_DIAG_MAX;
    if (chrisc_diag_push(&result, CHRIS_SEV_ERROR, 1, "C.CC", 1, 1, 1, 1, "x")) {
        return fail("diagnostic cap");
    }
    return 0;
}

int main(void) {
    if (round_trip() || compile_file_ids()) {
        return 1;
    }
    puts("test_cdbg: ok");
    return 0;
}
