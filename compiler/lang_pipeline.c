/* LEARN:WS64-W08 */
#include "lang_pipeline.h"
#include "bench.h"
#include "chrisc/chrisc.h"
#include "clvm/clasm.h"
#include "clvm/clvm.h"
#include "cfs.h"
#include "fs.h"
#include "gfx2d.h"
#include "gfx_slot.h"
#include "graphics.h"
#include "math3d.h"
#include "tex.h"
#include "clvm_sys.h"
#include "app_window.h"
#include "storage.h"
#include "task.h"
#include "ui.h"
#include "jit/jit.h"
#include "jit/jit_compile.h"

#define LANG_SOURCE_MAX 262144
#define LANG_FILE_MAX (CLVM_HEADER_SIZE + CLVM_MAX_CODE)
#define LANG_NAME_MAX 96
#define LANG_PIXELS (CLVM_SYS_GAME_W * CLVM_SYS_GAME_H)
typedef struct LangSlot {
    int used;
    uint8_t file[LANG_FILE_MAX];
    size_t file_size;
    ClvmVm vm;
    uint32_t pixels[LANG_PIXELS];
    ClvmGfxCtx gfx;
    char name[LANG_NAME_MAX];
    int task_id;
    int gfx_slot_id;
    int fullscreen;
    JitBuf jit;
    JitFn jit_fn;
    int use_jit;
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

static void scopy(char *dst, int cap, const char *src) {
    int i = 0;
    if (cap < 1) {
        return;
    }
    if (!src) {
        dst[0] = 0;
        return;
    }
    while (src[i] && i < cap - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = 0;
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

static int starts_src(const char *s) {
    return s[0] == 'S' && s[1] == 'R' && s[2] == 'C' && s[3] == '/';
}

static int basename_start(const char *s) {
    int n = slen(s);
    int i;
    int slash = 0;
    for (i = 0; i < n; ++i) {
        if (s[i] == '/') {
            slash = i + 1;
        }
    }
    return slash;
}

static int output_name(const char *in, char out[LANG_NAME_MAX]) {
    int n = slen(in);
    int base;
    int i;
    int dst = 0;
    if (n < 4) {
        return 0;
    }
    if (starts_src(in)) {
        out[dst++] = 'B';
        out[dst++] = 'I';
        out[dst++] = 'N';
        out[dst++] = '/';
        base = basename_start(in);
        n = slen(in + base);
        in = in + base;
        if (n < 4) {
            return 0;
        }
        for (i = 0; i < n - 3 && dst < LANG_NAME_MAX - 4; ++i) {
            out[dst++] = in[i];
        }
        out[dst++] = 'C';
        out[dst++] = 'L';
        out[dst++] = 'V';
        out[dst] = 0;
        return 1;
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
        slots[i].task_id = -1;
        slots[i].gfx_slot_id = -1;
        slots[i].fullscreen = 0;
        slots[i].name[0] = 0;
        slots[i].use_jit = 0;
        slots[i].jit_fn = NULL;
        slots[i].jit.phys = 0;
        slots[i].gfx.pixels = slots[i].pixels;
        slots[i].gfx.zbuf = 0;
        slots[i].gfx.w = CLVM_SYS_GAME_W;
        slots[i].gfx.h = CLVM_SYS_GAME_H;
        slots[i].gfx.slot_id = -1;
    }
}

static void lang_slot_release_gfx(int i) {
    int id = slots[i].gfx.slot_id;
    if (id < 0)
        id = slots[i].gfx_slot_id;
    if (id >= 0)
        gfx_slot_free(id);
    slots[i].gfx_slot_id = -1;
    slots[i].gfx.pixels = slots[i].pixels;
    slots[i].gfx.zbuf = 0;
    slots[i].gfx.w = CLVM_SYS_GAME_W;
    slots[i].gfx.h = CLVM_SYS_GAME_H;
    slots[i].gfx.slot_id = -1;
    slots[i].fullscreen = 0;
}

static int lang_setup_viewport(int i, const ClvmImage *image, const char *name) {
    int w;
    int h;
    uint32_t *pix;
    uint32_t *zb;
    int slot;
    (void)name;
    lang_slot_release_gfx(i);
    clvm_gfx_native_size(&w, &h);
    if ((image->flags & CLVM_FLAG_GAME) != 0) {
        slot = gfx_slot_alloc(w, h, &pix, &zb);
        if (slot < 0) {
            w = 1280;
            h = 720;
            slot = gfx_slot_alloc(w, h, &pix, &zb);
        }
        if (slot >= 0) {
            slots[i].gfx_slot_id = slot;
            slots[i].gfx.pixels = pix;
            slots[i].gfx.zbuf = zb;
            slots[i].gfx.w = w;
            slots[i].gfx.h = h;
            slots[i].gfx.slot_id = slot;
            slots[i].fullscreen = 1;
            tex_init();
            math3d_cam_reset();
            math3d_set_screen(w, h);
            return 1;
        }
    }
    slots[i].gfx.pixels = slots[i].pixels;
    slots[i].gfx.zbuf = 0;
    slots[i].gfx.w = CLVM_SYS_GAME_W;
    slots[i].gfx.h = CLVM_SYS_GAME_H;
    slots[i].gfx.slot_id = -1;
    slots[i].fullscreen = 0;
    return 1;
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
    {
        int rc = fs_write(e->name, source_buffer, n);
        char buf[80];
        if (rc < 0) {
            fs_err_status(buf, 80, rc, "save: ");
            status(e, buf);
            return 0;
        }
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
    if (!file_size) {
        status(e, "compile: empty image");
        return 0;
    }
    {
        int rc = fs_write(name, file_buffer, (int)file_size);
        char buf[80];
        if (rc < 0) {
            fs_err_status(buf, 80, rc, "compile: ");
            status(e, buf);
            return 0;
        }
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

static int lang_run_internal(Editor *e, const char *name, int use_jit) {
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
    if (storage_ready() && storage_cfs()) {
        if (cfs_perm(storage_cfs(), name, CFS_PERM_EXEC) != CFS_OK) {
            status(e, "no exec");
            return 0;
        }
    }
    slots[i].file_size = (size_t)n;
    load = clvm_parse(slots[i].file, slots[i].file_size, &image);
    if (load != CL_LOAD_OK) {
        status(e, clvm_load_error(load));
        return 0;
    }
    if (!lang_setup_viewport(i, &image, name)) {
        status(e, "run: gfx slot failed");
        return 0;
    }
    gfx2d_clear(slots[i].gfx.pixels, slots[i].gfx.w, slots[i].gfx.h, 0);
    clvm_vm_init(&slots[i].vm, &image, system_fn, &slots[i].gfx);
    slots[i].use_jit = 0;
    slots[i].jit_fn = NULL;
    if (slots[i].jit.phys != 0) {
        jit_free(&slots[i].jit);
    }
    if (use_jit) {
        if (jit_compile_image(&image, &slots[i].jit, &slots[i].jit_fn) != 0) {
            status(e, "jit compile failed");
            return 0;
        }
        slots[i].use_jit = 1;
    }
    scopy(slots[i].name, LANG_NAME_MAX, name);
    slots[i].task_id = -1;
    slots[i].used = 1;
    app_window_open(i, name);
    status(e, use_jit ? "running jit" : "running");
    return 1;
}

int lang_run(Editor *e, const char *name) {
    return lang_run_internal(e, name, 0);
}

int lang_run_jit(Editor *e, const char *name) {
    return lang_run_internal(e, name, 1);
}

int lang_compile_run(Editor *e) {
    char name[LANG_NAME_MAX];
    if (!lang_compile(e) || !output_name(e->name, name)) {
        return 0;
    }
    return lang_run(e, name);
}

int lang_compile_run_jit(Editor *e) {
    char name[LANG_NAME_MAX];
    if (!lang_compile(e) || !output_name(e->name, name)) {
        return 0;
    }
    return lang_run_jit(e, name);
}

void lang_tick(uint32_t now) {
    int i;

    bench_frame_tick();
    for (i = 0; i < LANG_VM_SLOTS; ++i) {
        if (slots[i].used) {
            ClvmStepResult r;
            clvm_vm_wake(&slots[i].vm, now);
            if (slots[i].use_jit && slots[i].jit_fn != NULL) {
                jit_set_sys_context(&slots[i].vm, &slots[i].gfx);
                r = slots[i].jit_fn(&slots[i].vm, LANG_VM_BUDGET, now);
            } else {
                r = clvm_step(&slots[i].vm, LANG_VM_BUDGET);
            }
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

int lang_kill(int slot) {
    if (slot < 0 || slot >= LANG_VM_SLOTS) {
        return 0;
    }
    if (slots[slot].jit.phys != 0) {
        jit_free(&slots[slot].jit);
    }
    lang_slot_release_gfx(slot);
    slots[slot].used = 0;
    slots[slot].task_id = -1;
    slots[slot].name[0] = 0;
    slots[slot].use_jit = 0;
    slots[slot].jit_fn = NULL;
    return 1;
}

int lang_slot_used(int slot) {
    if (slot < 0 || slot >= LANG_VM_SLOTS) {
        return 0;
    }
    return slots[slot].used;
}

const char *lang_slot_name(int slot) {
    if (slot < 0 || slot >= LANG_VM_SLOTS || !slots[slot].used) {
        return "";
    }
    return slots[slot].name;
}

uint32_t *lang_slot_pixels(int slot) {
    if (slot < 0 || slot >= LANG_VM_SLOTS) {
        return 0;
    }
    return slots[slot].gfx.pixels;
}

int lang_slot_w(int slot) {
    if (slot < 0 || slot >= LANG_VM_SLOTS) {
        return CLVM_SYS_GAME_W;
    }
    return slots[slot].gfx.w;
}

int lang_slot_h(int slot) {
    if (slot < 0 || slot >= LANG_VM_SLOTS) {
        return CLVM_SYS_GAME_H;
    }
    return slots[slot].gfx.h;
}

int lang_slot_fullscreen(int slot) {
    if (slot < 0 || slot >= LANG_VM_SLOTS) {
        return 0;
    }
    return slots[slot].fullscreen;
}

int lang_slot_task(int slot) {
    if (slot < 0 || slot >= LANG_VM_SLOTS) {
        return -1;
    }
    return slots[slot].task_id;
}

void lang_bind_task(int slot, int task_id) {
    if (slot < 0 || slot >= LANG_VM_SLOTS) {
        return;
    }
    slots[slot].task_id = task_id;
}

int lang_find_slot_by_task(int task_id) {
    int i;
    for (i = 0; i < LANG_VM_SLOTS; ++i) {
        if (slots[i].used && slots[i].task_id == task_id) {
            return i;
        }
    }
    return -1;
}
