#include "lang_pipeline.h"
#include "chrisc/chrisc.h"
#include "clvm/clasm.h"
#include "clvm/clvm.h"
#include "fs.h"

#define LANG_SOURCE_MAX 32768
#define LANG_FILE_MAX (CLVM_HEADER_SIZE + CLVM_MAX_CODE)
#define LANG_NAME_MAX 32

typedef struct LangSlot {
    int used;
    uint8_t file[LANG_FILE_MAX];
    size_t file_size;
    ClvmVm vm;
} LangSlot;

static LangSlot slots[LANG_VM_SLOTS];
static char source_buffer[LANG_SOURCE_MAX];
static uint8_t code_buffer[CLVM_MAX_CODE];
static uint8_t file_buffer[LANG_FILE_MAX];
static ClvmSysFn system_fn;
static void *system_user;

static int slen(const char *s) {
    int n = 0;
    while (s[n]) {
        ++n;
    }
    return n;
}

static void status(Editor *e, const char *s) {
    ed_set_status(e, s);
}

static void append(char *out, int cap, int *n, const char *s) {
    int i = 0;
    while (s[i] && *n + 1 < cap) {
        out[(*n)++] = s[i++];
    }
    out[*n] = 0;
}

static void append_u(char *out, int cap, int *n, unsigned v) {
    char d[11];
    int k = 0;
    if (v == 0) {
        d[k++] = '0';
    }
    while (v && k < 10) {
        d[k++] = (char)('0' + v % 10);
        v /= 10;
    }
    while (k && *n + 1 < cap) {
        out[(*n)++] = d[--k];
    }
    out[*n] = 0;
}

static int suffix(const char *s, const char *ext) {
    int a = slen(s);
    int b = slen(ext);
    int i;
    if (a < b) {
        return 0;
    }
    for (i = 0; i < b; ++i) {
        if (s[a - b + i] != ext[i]) {
            return 0;
        }
    }
    return 1;
}

static int output_name(const char *in, char out[LANG_NAME_MAX]) {
    int n = slen(in);
    int base;
    int i;
    if (n < 4) {
        return 0;
    }
    base = n - 3;
    if (base + 3 >= LANG_NAME_MAX) {
        return 0;
    }
    for (i = 0; i < base; ++i) {
        out[i] = in[i];
    }
    out[base++] = 'C';
    out[base++] = 'L';
    out[base++] = 'V';
    out[base] = 0;
    return 1;
}

static void diag_status(Editor *e, int line, int col, const char *message) {
    int n = 0;
    e->status[0] = 0;
    append(e->status, 80, &n, "error ");
    append_u(e->status, 80, &n, (unsigned)line);
    append(e->status, 80, &n, ":");
    append_u(e->status, 80, &n, (unsigned)col);
    append(e->status, 80, &n, " ");
    append(e->status, 80, &n, message);
}

void lang_init(ClvmSysFn sys, void *user) {
    int i;
    system_fn = sys;
    system_user = user;
    for (i = 0; i < LANG_VM_SLOTS; ++i) {
        slots[i].used = 0;
    }
}

int lang_save(Editor *e) {
    int n;
    if (!e || !e->name[0]) {
        if (e) {
            status(e, "save: filename required");
        }
        return 0;
    }
    ed_get_text(e, source_buffer, sizeof(source_buffer));
    n = slen(source_buffer);
    if (n <= 0) {
        status(e, "save: empty source");
        return 0;
    }
    if (fs_write(e->name, source_buffer, n) < 0) {
        status(e, "save: filesystem full");
        return 0;
    }
    e->dirty = 0;
    status(e, "saved");
    return 1;
}

int lang_compile(Editor *e) {
    size_t file_size = 0;
    size_t code_size = 0;
    uint16_t entry = 0;
    char name[LANG_NAME_MAX];
    if (!e || (!suffix(e->name, ".CVA") && !suffix(e->name, ".CC"))) {
        if (e) {
            status(e, "compile: use .CVA or .CC");
        }
        return 0;
    }
    if (!lang_save(e) || !output_name(e->name, name)) {
        status(e, "compile: name too long");
        return 0;
    }
    if (suffix(e->name, ".CVA")) {
        ClasmResult r;
        if (!clasm_compile(source_buffer, (size_t)slen(source_buffer),
                           code_buffer, sizeof(code_buffer), &r)) {
            diag_status(e, r.diag.line, r.diag.column, r.diag.message);
            return 0;
        }
        code_size = r.code_size;
        entry = r.entry;
    } else {
        ChrisResult r;
        if (!chrisc_compile(source_buffer, (size_t)slen(source_buffer),
                            code_buffer, sizeof(code_buffer), &r)) {
            diag_status(e, r.diag.line, r.diag.column, r.diag.message);
            return 0;
        }
        code_size = r.code_size;
        entry = r.entry;
    }
    file_size = clvm_write_image(file_buffer, sizeof(file_buffer), CLVM_FLAG_GAME,
                                 entry, code_buffer, code_size);
    if (!file_size || fs_write(name, file_buffer, (int)file_size) < 0) {
        status(e, "compile: cannot write .CLV");
        return 0;
    }
    {
        int n = 0;
        e->status[0] = 0;
        append(e->status, 80, &n, name);
        append(e->status, 80, &n, " compiled, ");
        append_u(e->status, 80, &n, (unsigned)code_size);
        append(e->status, 80, &n, " bytes");
    }
    return 1;
}

int lang_run(Editor *e, const char *name) {
    int i;
    int n;
    ClvmImage image;
    ClvmLoadError load;
    for (i = 0; i < LANG_VM_SLOTS && slots[i].used; ++i) {
    }
    if (i == LANG_VM_SLOTS) {
        status(e, "run: all VM slots busy");
        return 0;
    }
    n = fs_read(name, slots[i].file, sizeof(slots[i].file));
    if (n < 0) {
        status(e, "run: .CLV not found");
        return 0;
    }
    slots[i].file_size = (size_t)n;
    load = clvm_parse(slots[i].file, slots[i].file_size, &image);
    if (load != CL_LOAD_OK) {
        status(e, clvm_load_error(load));
        return 0;
    }
    clvm_vm_init(&slots[i].vm, &image, system_fn, system_user);
    slots[i].used = 1;
    status(e, "running");
    return 1;
}

int lang_compile_run(Editor *e) {
    char name[LANG_NAME_MAX];
    if (!lang_compile(e) || !output_name(e->name, name)) {
        return 0;
    }
    return lang_run(e, name);
}

void lang_tick(uint32_t now) {
    int i;
    for (i = 0; i < LANG_VM_SLOTS; ++i) {
        if (slots[i].used) {
            ClvmStepResult r;
            clvm_vm_wake(&slots[i].vm, now);
            r = clvm_step(&slots[i].vm, LANG_VM_BUDGET);
            if (r == CLVM_STEP_HALT || r == CLVM_STEP_FAULT)
                slots[i].used = 0;
        }
    }
}

int lang_active_count(void) {
    int i;
    int n = 0;
    for (i = 0; i < LANG_VM_SLOTS; ++i) {
        if (slots[i].used) {
            ++n;
        }
    }
    return n;
}

