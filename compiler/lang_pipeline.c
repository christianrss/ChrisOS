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
#include "cla/cla.h"
#include "heap.h"
#include "gc/gc.h"
#include "jit/jit.h"
#include "jit/jit_compile.h"

#define LANG_SOURCE_MAX 4194304
#define LANG_CODE_MAX (4u * 1024u * 1024u)
#define LANG_FILE_MAX (CLVM_HEADER_SIZE_V2 + LANG_CODE_MAX)
#define LANG_NAME_MAX 96
#define LANG_PIXELS (CLVM_SYS_GAME_W * CLVM_SYS_GAME_H)
#define LANG_LST_MAX 128
#define LANG_EVQ 8
typedef struct LangSlot {
    int used;
    uint8_t *file;
    size_t file_cap;
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
    uint8_t *heap_ram;
    uint64_t heap_ram_sz;
    int debug_on;
    int paused;
    int step_one;
    uint32_t breakpoints[32];
    int nbreak;
    uint32_t map_pc[CHRIS_MAP_MAX];
    uint16_t map_line[CHRIS_MAP_MAX];
    int map_n;
    int step_line;
    uint16_t last_line;
    int ev_key_q[LANG_EVQ];
    int ev_text_q[LANG_EVQ];
    int ev_key_n;
    int ev_key_r;
    int ev_text_n;
    int ev_text_r;
    int dying;
} LangSlot;

static LangSlot slots[LANG_VM_SLOTS];
static char source_buffer[LANG_SOURCE_MAX];
static uint8_t code_buffer[LANG_CODE_MAX];
static uint8_t file_buffer[LANG_FILE_MAX];
static ChrisResult g_chris_result;
static char g_last_clv[LANG_NAME_MAX];
static char g_last_err[160];
static int g_want_debug;
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

static int last_dot_at(const char *s);

static void status(Editor *e, const char *s) {
    if (e) {
        ed_set_status(e, s);
    }
}

static void slot_ev_reset(int i) {
    if (i < 0 || i >= LANG_VM_SLOTS) {
        return;
    }
    slots[i].ev_key_n = 0;
    slots[i].ev_key_r = 0;
    slots[i].ev_text_n = 0;
    slots[i].ev_text_r = 0;
}

static int replace_ext(const char *in, const char *ext, char out[LANG_NAME_MAX]) {
    int dot;
    int i;
    if (!in || !ext || !out) {
        return 0;
    }
    scopy(out, LANG_NAME_MAX, in);
    dot = last_dot_at(out);
    if (dot < 0) {
        return 0;
    }
    i = 0;
    while (ext[i] && dot + 1 + i < LANG_NAME_MAX - 1) {
        out[dot + 1 + i] = ext[i];
        i++;
    }
    out[dot + 1 + i] = 0;
    return 1;
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

static char lowc(char c) {
    if (c >= 'A' && c <= 'Z') {
        return (char)(c + ('a' - 'A'));
    }
    return c;
}

static int suffix(const char *s, const char *ext) {
    int a = slen(s);
    int b = slen(ext);
    int i;
    if (a < b) {
        return 0;
    }
    for (i = 0; i < b; ++i) {
        if (lowc(s[a - b + i]) != lowc(ext[i])) {
            return 0;
        }
    }
    return 1;
}

static int slot_ensure_file(int i) {
    if (i < 0 || i >= LANG_VM_SLOTS)
        return 0;
    if (slots[i].file)
        return 1;
    slots[i].file = (uint8_t *)kmalloc(LANG_FILE_MAX);
    if (!slots[i].file)
        return 0;
    slots[i].file_cap = LANG_FILE_MAX;
    return 1;
}

static int starts_src(const char *s) {
    return s && (s[0] == 'S' || s[0] == 's') &&
           (s[1] == 'R' || s[1] == 'r') &&
           (s[2] == 'C' || s[2] == 'c') && s[3] == '/';
}

static int last_dot_at(const char *s) {
    int i;
    int d = -1;
    if (!s) {
        return -1;
    }
    for (i = 0; s[i]; ++i) {
        if (s[i] == '.') {
            d = i;
        }
    }
    return d;
}

static void clv_to_map(const char *clv, char mapn[LANG_NAME_MAX]) {
    int k = 0;
    int dot;
    if (!clv || !mapn) {
        if (mapn) {
            mapn[0] = 0;
        }
        return;
    }
    while (clv[k] && k + 1 < LANG_NAME_MAX) {
        mapn[k] = clv[k];
        ++k;
    }
    mapn[k] = 0;
    dot = last_dot_at(mapn);
    if (dot >= 0 && dot + 4 < LANG_NAME_MAX) {
        int upper_ext = mapn[dot + 1] >= 'A' && mapn[dot + 1] <= 'Z';
        mapn[dot] = '.';
        mapn[dot + 1] = (char)(upper_ext ? 'M' : 'm');
        mapn[dot + 2] = (char)(upper_ext ? 'A' : 'a');
        mapn[dot + 3] = (char)(upper_ext ? 'P' : 'p');
        mapn[dot + 4] = 0;
    }
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
    int n;
    int dot;
    int i;
    int dst = 0;
    int upper_ext;
    const char *stem;
    if (!in || !out) {
        return 0;
    }
    n = slen(in);
    if (n < 2) {
        return 0;
    }
    stem = in;
    if (starts_src(in)) {
        if (in[0] >= 'a') {
            out[dst++] = 'b';
            out[dst++] = 'i';
            out[dst++] = 'n';
            out[dst++] = '/';
        } else {
            out[dst++] = 'B';
            out[dst++] = 'I';
            out[dst++] = 'N';
            out[dst++] = '/';
        }
        stem = in + basename_start(in);
    }
    dot = last_dot_at(stem);
    if (dot <= 0) {
        return 0;
    }
    upper_ext = stem[dot + 1] >= 'A' && stem[dot + 1] <= 'Z';
    if (dst + dot + 4 >= LANG_NAME_MAX) {
        return 0;
    }
    for (i = 0; i < dot; ++i) {
        out[dst++] = stem[i];
    }
    out[dst++] = '.';
    if (upper_ext) {
        out[dst++] = 'C';
        out[dst++] = 'L';
        out[dst++] = 'V';
    } else {
        out[dst++] = 'c';
        out[dst++] = 'l';
        out[dst++] = 'v';
    }
    out[dst] = 0;
    return 1;
}

static void clear_last_lang(void) {
    g_last_clv[0] = 0;
    g_last_err[0] = 0;
}

static void set_err_diag(const ChrisDiag *d) {
    int n = 0;
    g_last_err[0] = 0;
    if (!d) {
        return;
    }
    if (d->file[0]) {
        append(g_last_err, (int)sizeof(g_last_err), &n, d->file);
        append(g_last_err, (int)sizeof(g_last_err), &n, ":");
    }
    append_u(g_last_err, (int)sizeof(g_last_err), &n, (unsigned)d->line);
    append(g_last_err, (int)sizeof(g_last_err), &n, ":");
    append_u(g_last_err, (int)sizeof(g_last_err), &n, (unsigned)d->column);
    append(g_last_err, (int)sizeof(g_last_err), &n, " ");
    append(g_last_err, (int)sizeof(g_last_err), &n, d->message);
}

const char *lang_last_clv(void) {
    return g_last_clv;
}

const char *lang_last_error(void) {
    return g_last_err;
}

static int emit_game_clv(const char *outn) {
    size_t code_size;
    uint32_t entry;
    size_t file_size;
    int n;
    if (!outn || !outn[0]) {
        return 0;
    }
    scopy(g_last_clv, LANG_NAME_MAX, outn);
    code_size = g_chris_result.code_size;
    entry = g_chris_result.entry;
    lang_write_map(outn, &g_chris_result);
    if (code_size > 65535u || entry > 65535u)
        file_size = clvm_write_image_v2(file_buffer, sizeof(file_buffer),
                                       CLVM_FLAG_GAME, entry, 0,
                                       code_buffer, code_size);
    else
        file_size = clvm_write_image(file_buffer, sizeof(file_buffer),
                                    CLVM_FLAG_GAME, (uint16_t)entry,
                                    code_buffer, code_size);
    if (!file_size) {
        n = 0;
        append(g_last_err, (int)sizeof(g_last_err), &n, "clv image fail");
        return 0;
    }
    if (fs_write(outn, file_buffer, (int)file_size) < 0) {
        n = 0;
        append(g_last_err, (int)sizeof(g_last_err), &n, "clv write fail");
        return 0;
    }
    return 1;
}

static int chrisc_fs_read(void *user, const char *path, char *out, int cap) {
    int n;
    (void)user;
    if (!path || !out || cap < 2) {
        return -1;
    }
    n = fs_read(path, out, cap - 1);
    if (n < 0) {
        return -1;
    }
    out[n] = 0;
    return n;
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

static void chris_diag_status(Editor *e, const ChrisDiag *d) {
    int n = 0;
    e->status[0] = 0;
    if (d->file[0]) {
        append(e->status, 80, &n, d->file);
        append(e->status, 80, &n, ":");
    }
    append_u(e->status, 80, &n, (unsigned)d->line);
    append(e->status, 80, &n, ":");
    append_u(e->status, 80, &n, (unsigned)d->column);
    append(e->status, 80, &n, " ");
    append(e->status, 80, &n, d->message);
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
        slots[i].heap_ram = 0;
        slots[i].heap_ram_sz = 0;
        slots[i].debug_on = 0;
        slots[i].paused = 0;
        slots[i].step_one = 0;
        slots[i].nbreak = 0;
        slots[i].map_n = 0;
        slots[i].step_line = 0;
        slots[i].last_line = 0;
        slots[i].gfx.pixels = slots[i].pixels;
        slots[i].gfx.zbuf = 0;
        slots[i].gfx.w = CLVM_SYS_GAME_W;
        slots[i].gfx.h = CLVM_SYS_GAME_H;
        slots[i].gfx.slot_id = -1;
    }
}

#define CLVM_HEAP_RESERVE (64ull * 1024ull * 1024ull)
#define CLVM_SLOT_RAM_DEFAULT (256ull * 1024ull * 1024ull)

static void lang_free_slot_ram(int i) {
    if (slots[i].heap_ram) {
        kfree(slots[i].heap_ram);
        slots[i].heap_ram = 0;
        slots[i].heap_ram_sz = 0;
    }
}

static void lang_attach_slot_ram(int i, const ClvmImage *image) {
    uint64_t cap;
    uint64_t want;
    uint8_t *p;
    uint64_t n;

    lang_free_slot_ram(i);
    cap = heap_free_bytes();
    if (cap > CLVM_HEAP_RESERVE)
        cap -= CLVM_HEAP_RESERVE;
    else
        cap = 0;
    want = image && image->mem_hint ? (uint64_t)image->mem_hint : CLVM_SLOT_RAM_DEFAULT;
    if (want < CLVM_MEMORY_SIZE)
        want = CLVM_MEMORY_SIZE;
    if (want > cap && cap >= CLVM_MEMORY_SIZE)
        want = cap;
    p = (uint8_t *)kmalloc(want);
    if (!p)
        return;
    for (n = 0; n < want; ++n)
        p[n] = 0;
    if (slots[i].vm.memory) {
        uint64_t copy = slots[i].vm.mem_size;
        if (copy > want)
            copy = want;
        for (n = 0; n < copy; ++n)
            p[n] = slots[i].vm.memory[n];
    }
    slots[i].heap_ram = p;
    slots[i].heap_ram_sz = want;
    clvm_vm_set_memory(&slots[i].vm, p, want);
}

static void lang_safepoint(ClvmVm *vm) {
    (void)vm;
    gc_poll();
}

static uint16_t lang_line_at(const LangSlot *s, uint32_t pc) {
    int i;
    uint16_t line = 0;
    for (i = 0; i < s->map_n; ++i) {
        if (s->map_pc[i] <= pc)
            line = s->map_line[i];
        else
            break;
    }
    return line;
}

static void lang_load_map(int slot, const char *clv) {
    char mapn[LANG_NAME_MAX];
    char buf[8192];
    int n;
    int i;
    slots[slot].map_n = 0;
    slots[slot].step_line = 0;
    if (!clv)
        return;
    clv_to_map(clv, mapn);
    n = fs_read(mapn, buf, (int)sizeof(buf) - 1);
    if (n < 0)
        return;
    buf[n] = 0;
    i = 0;
    while (i < n && slots[slot].map_n < CHRIS_MAP_MAX) {
        unsigned pc = 0;
        unsigned line = 0;
        while (buf[i] == ' ' || buf[i] == '\n' || buf[i] == '\r')
            i++;
        if (buf[i] == '0' && (buf[i + 1] == 'x' || buf[i + 1] == 'X'))
            i += 2;
        while ((buf[i] >= '0' && buf[i] <= '9') ||
               (buf[i] >= 'a' && buf[i] <= 'f') ||
               (buf[i] >= 'A' && buf[i] <= 'F')) {
            unsigned v = (unsigned)buf[i];
            if (v >= '0' && v <= '9')
                v -= '0';
            else if (v >= 'a')
                v = v - 'a' + 10;
            else
                v = v - 'A' + 10;
            pc = (pc << 4) | v;
            i++;
        }
        while (buf[i] == ' ')
            i++;
        while (buf[i] >= '0' && buf[i] <= '9') {
            line = line * 10u + (unsigned)(buf[i] - '0');
            i++;
        }
        slots[slot].map_pc[slots[slot].map_n] = pc;
        slots[slot].map_line[slots[slot].map_n] = (uint16_t)line;
        slots[slot].map_n++;
        while (buf[i] && buf[i] != '\n')
            i++;
        if (buf[i] == '\n')
            i++;
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
    w = CLVM_SYS_GAME_W;
    h = CLVM_SYS_GAME_H;
    if ((image->flags & CLVM_FLAG_GAME) != 0) {
        slot = gfx_slot_alloc(w, h, &pix, &zb);
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
    uint32_t entry = 0;
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
        if (!chrisc_compile_ex(e->name, source_buffer, (size_t)slen(source_buffer),
                               chrisc_fs_read, 0, code_buffer, sizeof(code_buffer),
                               &g_chris_result)) {
            chris_diag_status(e, &g_chris_result.diag);
            return 0;
        }
        code_size = g_chris_result.code_size;
        entry = g_chris_result.entry;
        lang_write_map(name, &g_chris_result);
    }
    if (code_size > 65535u || entry > 65535u)
        file_size = clvm_write_image_v2(file_buffer, sizeof(file_buffer),
                                       CLVM_FLAG_GAME, entry, 0,
                                       code_buffer, code_size);
    else
        file_size = clvm_write_image(file_buffer, sizeof(file_buffer),
                                    CLVM_FLAG_GAME, (uint16_t)entry,
                                    code_buffer, code_size);
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
        scopy(g_last_clv, LANG_NAME_MAX, name);
    }
    {
        ClaImage img;
        char cla[LANG_NAME_MAX];
        int k = 0;
        int ncla;
        while (name[k] && k + 1 < LANG_NAME_MAX) {
            cla[k] = name[k];
            k++;
        }
        cla[k] = 0;
        if (k > 3) {
            cla[k - 3] = 'C';
            cla[k - 2] = 'L';
            cla[k - 1] = 'A';
        }
        {
            unsigned z;
            uint8_t *p = (uint8_t *)&img;
            for (z = 0; z < sizeof(img); ++z)
                p[z] = 0;
        }
        img.name[0] = 'M';
        img.name[1] = 0;
        img.version = 1;
        img.nmethods = 1;
        img.methods[0].name[0] = 'm';
        img.methods[0].name[1] = 'a';
        img.methods[0].name[2] = 'i';
        img.methods[0].name[3] = 'n';
        img.methods[0].rva = 0;
        img.methods[0].size = (uint32_t)code_size;
        img.methods[0].sig.ret = IL_I4;
        img.il = code_buffer;
        img.il_size = (uint32_t)code_size;
        ncla = cla_write(file_buffer, sizeof(file_buffer), &img);
        if (ncla > 0)
            (void)fs_write(cla, file_buffer, ncla);
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
    if (!slot_ensure_file(i)) {
        status(e, "run: no memory");
        return 0;
    }
    n = fs_read(name, slots[i].file, (int)slots[i].file_cap);
    if (n < 0) {
        int k = 0;
        e->status[0] = 0;
        append(e->status, 80, &k, "run: not found ");
        append(e->status, 80, &k, name ? name : "");
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
    slots[i].vm.on_safepoint = lang_safepoint;
    lang_attach_slot_ram(i, &image);
    lang_load_map(i, name);
    slots[i].use_jit = 0;
    slots[i].jit_fn = NULL;
    if (slots[i].jit.phys != 0) {
        jit_free(&slots[i].jit);
    }
    if (use_jit && !g_want_debug) {
        if (jit_compile_image(&image, &slots[i].jit, &slots[i].jit_fn) != 0) {
            status(e, "jit compile failed");
            return 0;
        }
        slots[i].use_jit = 1;
    }
    scopy(slots[i].name, LANG_NAME_MAX, name);
    slots[i].task_id = -1;
    slots[i].used = 1;
    slots[i].debug_on = g_want_debug;
    slots[i].paused = g_want_debug;
    slots[i].step_one = 0;
    slots[i].nbreak = 0;
    slots[i].step_line = 0;
    slots[i].last_line = lang_line_at(&slots[i], slots[i].vm.pc);
    slot_ev_reset(i);
    slots[i].dying = 0;
    app_window_open(i, name);
    status(e, use_jit ? "running jit" : "running");
    return 1;
}

int lang_compile_path(const char *src) {
    char outn[LANG_NAME_MAX];
    if (!src || !src[0]) {
        return 0;
    }
    if (suffix(src, ".LST")) {
        return lang_compile_list(src);
    }
    if (!output_name(src, outn)) {
        return 0;
    }
    return lang_compile_file(src, outn);
}

int lang_run_path(const char *name) {
    static Editor dummy;
    char probe[1];
    char alt[LANG_NAME_MAX];

    dummy.name[0] = 0;
    dummy.status[0] = 0;
    if (!name || !name[0]) {
        return 0;
    }
    if (fs_read(name, probe, 1) < 0) {
        if (replace_ext(name, "LST", alt) && lang_compile_list(alt)) {
            return lang_run_internal(&dummy, name, 0);
        }
        if (replace_ext(name, "CC", alt) && lang_compile_file(alt, name)) {
            return lang_run_internal(&dummy, name, 0);
        }
        return 0;
    }
    return lang_run_internal(&dummy, name, 0);
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

int lang_compile_file(const char *src_path, const char *clv_path) {
    size_t file_size = 0;
    size_t code_size = 0;
    uint32_t entry = 0;
    int n;

    if (!src_path || !clv_path) {
        return 0;
    }
    if (!suffix(src_path, ".CVA") && !suffix(src_path, ".CC")) {
        return 0;
    }
    n = fs_read(src_path, source_buffer, (int)sizeof(source_buffer) - 1);
    if (n < 0) {
        return 0;
    }
    source_buffer[n] = 0;
    if (suffix(src_path, ".CVA")) {
        ClasmResult r;
        if (!clasm_compile(source_buffer, (size_t)n, code_buffer,
                           sizeof(code_buffer), &r)) {
            return 0;
        }
        code_size = r.code_size;
        entry = r.entry;
    } else {
        if (!chrisc_compile_ex(src_path, source_buffer, (size_t)n, chrisc_fs_read, 0,
                               code_buffer, sizeof(code_buffer), &g_chris_result)) {
            return 0;
        }
        code_size = g_chris_result.code_size;
        entry = g_chris_result.entry;
    }
    if (code_size > 65535u || entry > 65535u)
        file_size = clvm_write_image_v2(file_buffer, sizeof(file_buffer),
                                       CLVM_FLAG_GAME, entry, 0,
                                       code_buffer, code_size);
    else
        file_size = clvm_write_image(file_buffer, sizeof(file_buffer),
                                    CLVM_FLAG_GAME, (uint16_t)entry,
                                    code_buffer, code_size);
    if (!file_size) {
        return 0;
    }
    return fs_write(clv_path, file_buffer, (int)file_size) >= 0;
}

int lang_compile_many(const char **paths, int npaths) {
    char outn[LANG_NAME_MAX];
    clear_last_lang();
    if (!paths || npaths < 1)
        return 0;
    if (!output_name(paths[0], outn))
        return 0;
    if (!chrisc_compile_files(paths, npaths, chrisc_fs_read, 0, code_buffer,
                              sizeof(code_buffer), &g_chris_result)) {
        set_err_diag(&g_chris_result.diag);
        return 0;
    }
    return emit_game_clv(outn);
}

int lang_compile_list(const char *lst_path) {
    static char lst[16384];
    static char paths[LANG_LST_MAX][FS_PATH];
    const char *pp[LANG_LST_MAX];
    int n;
    int npaths = 0;
    int p;
    if (!lst_path)
        return 0;
    n = fs_read(lst_path, lst, (int)sizeof(lst) - 1);
    if (n < 0) {
        int k = 0;
        clear_last_lang();
        append(g_last_err, (int)sizeof(g_last_err), &k, "lst not found ");
        append(g_last_err, (int)sizeof(g_last_err), &k, lst_path);
        return 0;
    }
    lst[n] = 0;
    p = 0;
    while (p < n && npaths < LANG_LST_MAX) {
        int j = 0;
        while (p < n && (lst[p] == ' ' || lst[p] == '\t' || lst[p] == '\r' ||
                         lst[p] == '\n'))
            p++;
        if (p >= n)
            break;
        if (lst[p] == '#') {
            while (p < n && lst[p] != '\n')
                p++;
            continue;
        }
        while (p < n && lst[p] != '\n' && lst[p] != '\r' && j < FS_PATH - 1) {
            paths[npaths][j++] = lst[p++];
        }
        while (j > 0 && (paths[npaths][j - 1] == ' ' ||
                         paths[npaths][j - 1] == '\t'))
            j--;
        paths[npaths][j] = 0;
        if (j > 0) {
            pp[npaths] = paths[npaths];
            npaths++;
        }
        if (p < n && (lst[p] == '\n' || lst[p] == '\r'))
            p++;
    }
    if (npaths < 1)
        return 0;
    clear_last_lang();
    if (!chrisc_compile_files(pp, npaths, chrisc_fs_read, 0, code_buffer,
                              sizeof(code_buffer), &g_chris_result)) {
        set_err_diag(&g_chris_result.diag);
        return 0;
    }
    {
        char outn[LANG_NAME_MAX];
        if (!output_name(lst_path, outn)) {
            int k = 0;
            append(g_last_err, (int)sizeof(g_last_err), &k, "bad lst name");
            return 0;
        }
        return emit_game_clv(outn);
    }
}

int lang_splash_start(const char *name) {
    int i = LANG_SPLASH_SLOT;
    int n;
    ClvmImage image;
    ClvmLoadError load;

    if (!name) {
        return 0;
    }
    if (slots[i].used) {
        lang_kill(i);
    }
    if (!slot_ensure_file(i)) {
        return 0;
    }
    n = fs_read(name, slots[i].file, (int)slots[i].file_cap);
    if (n < 0) {
        return 0;
    }
    if (storage_ready() && storage_cfs()) {
        if (cfs_perm(storage_cfs(), name, CFS_PERM_EXEC) != CFS_OK) {
            return 0;
        }
    }
    slots[i].file_size = (size_t)n;
    load = clvm_parse(slots[i].file, slots[i].file_size, &image);
    if (load != CL_LOAD_OK) {
        return 0;
    }
    if (!lang_setup_viewport(i, &image, name)) {
        return 0;
    }
    gfx2d_clear(slots[i].gfx.pixels, slots[i].gfx.w, slots[i].gfx.h, 0);
    clvm_vm_init(&slots[i].vm, &image, system_fn, &slots[i].gfx);
    slots[i].vm.on_safepoint = lang_safepoint;
    lang_attach_slot_ram(i, &image);
    lang_load_map(i, name);
    slots[i].use_jit = 0;
    slots[i].jit_fn = NULL;
    if (slots[i].jit.phys != 0) {
        jit_free(&slots[i].jit);
    }
    scopy(slots[i].name, LANG_NAME_MAX, name);
    slots[i].task_id = -1;
    slots[i].used = 1;
    return 1;
}

void lang_splash_frame(uint32_t now) {
    int i = LANG_SPLASH_SLOT;
    int w;
    int h;
    int dx;
    int dy;
    uint32_t *pix;

    if (!slots[i].used) {
        return;
    }
    clvm_vm_wake(&slots[i].vm, now);
    (void)clvm_step(&slots[i].vm, LANG_VM_BUDGET);
    pix = slots[i].gfx.pixels;
    w = slots[i].gfx.w;
    h = slots[i].gfx.h;
    dx = (g_gfx.width - w) / 2;
    dy = (g_gfx.height - h) / 2;
    gfx_clear(0x00101828u);
    clvm_sys_blit_to(pix, dx, dy, w, h, w, h);
    gfx_present();
}

void lang_splash_stop(void) {
    lang_kill(LANG_SPLASH_SLOT);
}

void lang_tick(uint32_t now) {
    int i;

    bench_frame_tick();
    for (i = 0; i < LANG_VM_SLOTS; ++i) {
        if (slots[i].used) {
            ClvmStepResult r;
            int b;
            if (slots[i].paused && !slots[i].step_one && !slots[i].step_line)
                continue;
            clvm_vm_wake(&slots[i].vm, now);
            if (slots[i].debug_on) {
                for (b = 0; b < slots[i].nbreak; ++b) {
                    if (slots[i].breakpoints[b] == slots[i].vm.pc) {
                        slots[i].paused = 1;
                        slots[i].step_one = 0;
                        slots[i].step_line = 0;
                        break;
                    }
                }
                if (slots[i].paused && !slots[i].step_one && !slots[i].step_line)
                    continue;
            }
            if (slots[i].use_jit && slots[i].jit_fn != NULL && !slots[i].debug_on) {
                jit_set_sys_context(&slots[i].vm, &slots[i].gfx);
                r = slots[i].jit_fn(&slots[i].vm, LANG_VM_BUDGET, now);
            } else if (slots[i].debug_on && slots[i].step_line) {
                int k;
                uint16_t start = slots[i].last_line;
                r = CLVM_STEP_SLICE;
                for (k = 0; k < 512; ++k) {
                    r = clvm_step(&slots[i].vm, 1);
                    if (r == CLVM_STEP_HALT || r == CLVM_STEP_FAULT)
                        break;
                    if (lang_line_at(&slots[i], slots[i].vm.pc) != start)
                        break;
                }
                slots[i].step_line = 0;
                slots[i].paused = 1;
                slots[i].last_line = lang_line_at(&slots[i], slots[i].vm.pc);
            } else if (slots[i].debug_on && slots[i].step_one) {
                r = clvm_step(&slots[i].vm, 1);
                slots[i].step_one = 0;
                slots[i].paused = 1;
            } else {
                r = clvm_step(&slots[i].vm, LANG_VM_BUDGET);
            }
            if (r == CLVM_STEP_HALT || r == CLVM_STEP_FAULT || slots[i].dying) {
                lang_kill(i);
            }
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
    if (!slots[slot].used && !slots[slot].dying) {
        return 0;
    }
    if (slots[slot].jit.phys != 0) {
        jit_free(&slots[slot].jit);
    }
    lang_free_slot_ram(slot);
    lang_slot_release_gfx(slot);
    clvm_sys_close_slot(slots[slot].gfx.slot_id >= 0 ? slots[slot].gfx.slot_id
                                                    : slot);
    if (slots[slot].file) {
        kfree(slots[slot].file);
        slots[slot].file = 0;
        slots[slot].file_cap = 0;
    }
    slots[slot].used = 0;
    slots[slot].dying = 0;
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

int lang_find_slot_by_gfx(const void *gfx_ctx) {
    int i;
    if (!gfx_ctx) {
        return -1;
    }
    for (i = 0; i < LANG_VM_SLOTS; ++i) {
        if (slots[i].used && (const void *)&slots[i].gfx == gfx_ctx) {
            return i;
        }
    }
    return -1;
}

void lang_slot_push_key(int slot, int key) {
    int i;
    if (slot < 0 || slot >= LANG_VM_SLOTS || !slots[slot].used) {
        return;
    }
    if (slots[slot].ev_key_n >= LANG_EVQ) {
        return;
    }
    i = (slots[slot].ev_key_r + slots[slot].ev_key_n) % LANG_EVQ;
    slots[slot].ev_key_q[i] = key;
    slots[slot].ev_key_n++;
}

void lang_slot_push_text(int slot, int ch) {
    int i;
    if (slot < 0 || slot >= LANG_VM_SLOTS || !slots[slot].used) {
        return;
    }
    if (slots[slot].ev_text_n >= LANG_EVQ) {
        return;
    }
    i = (slots[slot].ev_text_r + slots[slot].ev_text_n) % LANG_EVQ;
    slots[slot].ev_text_q[i] = ch;
    slots[slot].ev_text_n++;
}

int lang_slot_take_key(int slot) {
    int k;
    if (slot < 0 || slot >= LANG_VM_SLOTS || !slots[slot].used) {
        return 0;
    }
    if (slots[slot].ev_key_n <= 0) {
        return 0;
    }
    k = slots[slot].ev_key_q[slots[slot].ev_key_r];
    slots[slot].ev_key_r = (slots[slot].ev_key_r + 1) % LANG_EVQ;
    slots[slot].ev_key_n--;
    return k;
}

int lang_slot_take_text(int slot) {
    int c;
    if (slot < 0 || slot >= LANG_VM_SLOTS || !slots[slot].used) {
        return 0;
    }
    if (slots[slot].ev_text_n <= 0) {
        return 0;
    }
    c = slots[slot].ev_text_q[slots[slot].ev_text_r];
    slots[slot].ev_text_r = (slots[slot].ev_text_r + 1) % LANG_EVQ;
    slots[slot].ev_text_n--;
    return c;
}

void lang_slot_request_close(int slot) {
    if (slot < 0 || slot >= LANG_VM_SLOTS) {
        return;
    }
    slots[slot].dying = 1;
}

void lang_write_map(const char *clv_path, const ChrisResult *r) {
    char mapn[LANG_NAME_MAX];
    char buf[8192];
    int n = 0;
    int i;
    if (!clv_path || !r)
        return;
    clv_to_map(clv_path, mapn);
    buf[0] = 0;
    for (i = 0; i < r->map_n && n + 32 < (int)sizeof(buf); ++i) {
        unsigned v = r->map[i].pc;
        unsigned line = r->map[i].line;
        char tmp[48];
        int t = 0;
        tmp[t++] = '0';
        tmp[t++] = 'x';
        {
            char hex[8];
            int h = 0;
            unsigned x = v;
            if (x == 0)
                hex[h++] = '0';
            while (x && h < 8) {
                hex[h++] = "0123456789abcdef"[x & 15];
                x >>= 4;
            }
            while (h--)
                tmp[t++] = hex[h];
        }
        tmp[t++] = ' ';
        {
            unsigned x = line;
            char dec[8];
            int d = 0;
            if (x == 0)
                dec[d++] = '0';
            while (x && d < 8) {
                dec[d++] = (char)('0' + (x % 10));
                x /= 10;
            }
            while (d--)
                tmp[t++] = dec[d];
        }
        tmp[t++] = '\n';
        tmp[t] = 0;
        {
            int c;
            for (c = 0; tmp[c] && n + 1 < (int)sizeof(buf); ++c)
                buf[n++] = tmp[c];
        }
    }
    buf[n] = 0;
    if (n > 0)
        (void)fs_write(mapn, buf, n);
}

void lang_debug_enable(int on) {
    g_want_debug = on;
}

void lang_debug_step(void) {
    int i;
    for (i = 0; i < LANG_VM_SLOTS; ++i) {
        if (slots[i].used && slots[i].debug_on) {
            slots[i].last_line = lang_line_at(&slots[i], slots[i].vm.pc);
            slots[i].step_line = 1;
            slots[i].step_one = 0;
            slots[i].paused = 0;
        }
    }
}

void lang_debug_continue(void) {
    int i;
    for (i = 0; i < LANG_VM_SLOTS; ++i) {
        if (slots[i].used && slots[i].debug_on) {
            slots[i].paused = 0;
            slots[i].step_one = 0;
        }
    }
}

int lang_debug_paused(void) {
    int i;
    for (i = 0; i < LANG_VM_SLOTS; ++i) {
        if (slots[i].used && slots[i].paused)
            return 1;
    }
    return 0;
}

uint32_t lang_debug_pc(void) {
    int i;
    for (i = 0; i < LANG_VM_SLOTS; ++i) {
        if (slots[i].used && slots[i].debug_on)
            return slots[i].vm.pc;
    }
    return 0;
}

int64_t lang_debug_stack(int i) {
    int s;
    for (s = 0; s < LANG_VM_SLOTS; ++s) {
        if (slots[s].used && slots[s].debug_on) {
            if (i < 0 || i >= (int)slots[s].vm.sp)
                return 0;
            return slots[s].vm.stack[slots[s].vm.sp - 1 - i];
        }
    }
    return 0;
}

int32_t lang_debug_mem(uint32_t addr) {
    int s;
    for (s = 0; s < LANG_VM_SLOTS; ++s) {
        if (slots[s].used && slots[s].debug_on) {
            const uint8_t *m = slots[s].vm.memory;
            if (!m || (uint64_t)addr + 4u > slots[s].vm.mem_size)
                return 0;
            return (int32_t)((uint32_t)m[addr] | ((uint32_t)m[addr + 1] << 8) |
                             ((uint32_t)m[addr + 2] << 16) |
                             ((uint32_t)m[addr + 3] << 24));
        }
    }
    return 0;
}

int lang_bp_add(uint32_t pc) {
    int s;
    for (s = 0; s < LANG_VM_SLOTS; ++s) {
        if (slots[s].used && slots[s].nbreak < 32) {
            slots[s].breakpoints[slots[s].nbreak++] = pc;
            return 1;
        }
    }
    return 0;
}

uint16_t lang_debug_line(void) {
    int s;
    for (s = 0; s < LANG_VM_SLOTS; ++s) {
        if (slots[s].used && slots[s].debug_on)
            return lang_line_at(&slots[s], slots[s].vm.pc);
    }
    return 0;
}

int lang_bp_toggle_line(int line) {
    int s;
    int i;
    uint32_t pc = 0;
    int found = 0;
    if (line < 1)
        return 0;
    for (s = 0; s < LANG_VM_SLOTS; ++s) {
        if (!slots[s].used)
            continue;
        for (i = 0; i < slots[s].map_n; ++i) {
            if ((int)slots[s].map_line[i] == line) {
                pc = slots[s].map_pc[i];
                found = 1;
                break;
            }
        }
        if (!found)
            continue;
        for (i = 0; i < slots[s].nbreak; ++i) {
            if (slots[s].breakpoints[i] == pc) {
                slots[s].breakpoints[i] = slots[s].breakpoints[slots[s].nbreak - 1];
                slots[s].nbreak--;
                return 0;
            }
        }
        if (slots[s].nbreak < 32) {
            slots[s].breakpoints[slots[s].nbreak++] = pc;
            return 1;
        }
    }
    return -1;
}
