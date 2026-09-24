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
#include "gfx_fast.h"
#include "clvm_sys.h"
#include "app_window.h"
#include "storage.h"
#include "task.h"
#include "ui.h"
#include "cla/cla.h"
#include "cls/cls.h"
#include "heap.h"
#include "gc/gc.h"
#include "proc.h"
#include "mm.h"
#include "debug/cdbg.h"
#include "debug/dbg_session.h"

static int path_is_driver(const char *name);
#include "bootinfo.h"
#include "jit/jit.h"
#include "jit/jit_compile.h"
#include "serial.h"
#include "pit.h"

#define LANG_SOURCE_MAX 4194304
#define LANG_CODE_MAX (4u * 1024u * 1024u)
#define LANG_FILE_MAX (CLVM_HEADER_SIZE_V2 + LANG_CODE_MAX)
#define LANG_NAME_MAX 96
#define LANG_PIXELS (CLVM_SYS_GAME_W * CLVM_SYS_GAME_H)
#define LANG_LST_MAX 128
#define LANG_EVQ 8
typedef struct LangSlot {
    int used;
    uint32_t caps;
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
    uint16_t map_file[CHRIS_MAP_MAX];
    int map_n;
    int step_line;
    int step_mode;
    int step_depth;
    uint16_t last_line;
    int ev_key_q[LANG_EVQ];
    int ev_text_q[LANG_EVQ];
    int ev_key_n;
    int ev_key_r;
    int ev_text_n;
    int ev_text_r;
    int dying;
    uint32_t *front_pixels;
    int front_w;
    int front_h;
    int proc_id;
    int user_ram;
    uint8_t checkpoint[256];
    int checkpoint_n;
    uint32_t checkpoint_off;
    char fn_name[16][24];
    uint32_t fn_pc[16];
    int fn_n;
    uint32_t watch;
} LangSlot;

static LangSlot slots[LANG_VM_SLOTS];
static char source_buffer[LANG_SOURCE_MAX];
static uint8_t code_buffer[LANG_CODE_MAX];
static uint8_t file_buffer[LANG_FILE_MAX];
static ChrisResult g_chris_result;
static char g_last_clv[LANG_NAME_MAX];
static char g_last_err[160];
static char g_app_arg[FS_PATH];
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

static int samestr(const char *a, const char *b) {
    int i = 0;
    if (!a || !b) {
        return 0;
    }
    while (a[i] && b[i] && a[i] == b[i]) {
        i++;
    }
    return a[i] == 0 && b[i] == 0;
}

static int lang_kill_by_name(const char *name) {
    int i;
    int killed = 0;
    if (!name || !name[0]) {
        return 0;
    }
    for (i = 0; i < LANG_VM_SLOTS; ++i) {
        if (!slots[i].used) {
            continue;
        }
        if (!samestr(slots[i].name, name)) {
            continue;
        }
        serial_puts("run: replacing ");
        serial_puts(name);
        serial_puts("\n");
        lang_kill(i);
        killed = 1;
    }
    return killed;
}

static int lang_raise_existing(const char *name) {
    int i;
    if (!name || !name[0]) {
        return 0;
    }
    for (i = 0; i < LANG_VM_SLOTS; ++i) {
        if (!slots[i].used) {
            continue;
        }
        if (!samestr(slots[i].name, name)) {
            continue;
        }
        if (slots[i].task_id >= 0) {
            task_raise(slots[i].task_id);
        }
        serial_puts("run: already running, raise ");
        serial_puts(name);
        serial_puts("\n");
        return 1;
    }
    return 0;
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

static void clv_to_cdbg(const char *clv, char outn[LANG_NAME_MAX]) {
    int k = 0;
    int dot;
    if (!clv || !outn) {
        if (outn) {
            outn[0] = 0;
        }
        return;
    }
    while (clv[k] && k + 1 < LANG_NAME_MAX) {
        outn[k] = clv[k];
        ++k;
    }
    outn[k] = 0;
    dot = last_dot_at(outn);
    if (dot >= 0 && dot + 5 < LANG_NAME_MAX) {
        int upper_ext = outn[dot + 1] >= 'A' && outn[dot + 1] <= 'Z';
        outn[dot] = '.';
        outn[dot + 1] = (char)(upper_ext ? 'C' : 'c');
        outn[dot + 2] = (char)(upper_ext ? 'D' : 'd');
        outn[dot + 3] = (char)(upper_ext ? 'B' : 'b');
        outn[dot + 4] = (char)(upper_ext ? 'G' : 'g');
        outn[dot + 5] = 0;
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
    {
        uint32_t mem_hint = 0;
        /* Doom-sized images need a large guest heap (zone + lumps). */
        if (code_size > 200000u) {
            mem_hint = 32u * 1024u * 1024u;
        }
        if (code_size > 65535u || entry > 65535u || mem_hint != 0)
            file_size = clvm_write_image_v2(file_buffer, sizeof(file_buffer),
                                           CLVM_FLAG_GAME, entry, mem_hint,
                                           code_buffer, code_size);
        else
            file_size = clvm_write_image(file_buffer, sizeof(file_buffer),
                                        CLVM_FLAG_GAME, (uint16_t)entry,
                                        code_buffer, code_size);
    }
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

static uint32_t g_cc_hb_tick;

static void lang_cc_pump(void) {
    uint32_t now;
    gc_poll();
    now = (uint32_t)ticks;
    if (now - g_cc_hb_tick >= 60u) {
        g_cc_hb_tick = now;
        serial_puts("cc: working...\n");
    }
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

static void chrisc_serial_progress(void *user, int index, int total,
                                   const char *path) {
    (void)user;
    if (index < 0) {
        int done = -index;
        if (done > total) {
            serial_puts("cc: emit\n");
        } else {
            serial_puts("cc: ok [");
            serial_write_u64((uint64_t)(uint32_t)done);
            serial_puts("/");
            serial_write_u64((uint64_t)(uint32_t)total);
            serial_puts("] ");
            serial_puts(path ? path : "?");
            serial_puts("\n");
        }
        lang_cc_pump();
        return;
    }
    serial_puts("cc: [");
    serial_write_u64((uint64_t)(uint32_t)(index + 1));
    serial_puts("/");
    serial_write_u64((uint64_t)(uint32_t)total);
    serial_puts("] ");
    serial_puts(path ? path : "?");
    serial_puts("\n");
    lang_cc_pump();
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
    g_app_arg[0] = 0;
    cls_runtime_init();
    for (i = 0; i < LANG_VM_SLOTS; ++i) {
        slots[i].used = 0;
        slots[i].task_id = -1;
        slots[i].gfx_slot_id = -1;
        slots[i].fullscreen = 0;
        slots[i].name[0] = 0;
        slots[i].use_jit = 0;
        slots[i].jit_fn = NULL;
        slots[i].jit.phys = 0;
        slots[i].jit.pages = 0;
        slots[i].jit.w = NULL;
        slots[i].jit.x = NULL;
        slots[i].heap_ram = 0;
        slots[i].heap_ram_sz = 0;
        slots[i].debug_on = 0;
        slots[i].paused = 0;
        slots[i].step_one = 0;
        slots[i].nbreak = 0;
        slots[i].map_n = 0;
        slots[i].step_line = 0;
        slots[i].step_mode = DBG_STEP_NONE;
        slots[i].step_depth = 0;
        slots[i].last_line = 0;
        slots[i].dying = 0;
        slots[i].front_pixels = 0;
        slots[i].front_w = 0;
        slots[i].front_h = 0;
        slots[i].gfx.pixels = slots[i].pixels;
        slots[i].gfx.zbuf = 0;
        slots[i].gfx.w = CLVM_SYS_GAME_W;
        slots[i].gfx.h = CLVM_SYS_GAME_H;
        slots[i].gfx.slot_id = -1;
    }
}

#define CLVM_HEAP_RESERVE (64ull * 1024ull * 1024ull)
#define CLVM_SLOT_RAM_DEFAULT ((uint64_t)CLVM_MEMORY_SIZE)

static void lang_free_slot_ram(int i) {
    if (slots[i].user_ram) {
        slots[i].heap_ram = 0;
        slots[i].heap_ram_sz = 0;
        slots[i].user_ram = 0;
        return;
    }
    if (slots[i].heap_ram) {
        kfree(slots[i].heap_ram);
        slots[i].heap_ram = 0;
        slots[i].heap_ram_sz = 0;
    }
}

static void lang_map_surface(int i) {
    const struct bootinfo *boot;
    uint64_t virt;
    uint64_t phys;
    int bytes;
    int pages;
    int p;
    if (slots[i].proc_id <= 0 || slots[i].gfx.pixels == 0)
        return;
    boot = bootinfo_get();
    if (!boot || boot->hhdm_offset == 0)
        return;
    virt = (uint64_t)(uintptr_t)slots[i].gfx.pixels & ~4095ull;
    if (virt < boot->hhdm_offset)
        return;
    phys = virt - boot->hhdm_offset;
    bytes = slots[i].gfx.w * slots[i].gfx.h * 4;
    if (bytes < 1)
        return;
    pages = (bytes + 4095) / 4096;
    if (pages > 64)
        pages = 64;
    for (p = 0; p < pages; ++p) {
        proc_map_user(slots[i].proc_id,
                      PROC_FB_VIRT + (uint64_t)p * 4096ull,
                      phys + (uint64_t)p * 4096ull, MM_PRESENT | MM_WRITE);
    }
}

static void mem_zero_fast(uint8_t *p, uint64_t n) {
    uint64_t i;
    uint32_t *w;
    uint64_t nw;
    if (!p || n == 0)
        return;
    while (n && ((uintptr_t)p & 3u)) {
        *p++ = 0;
        n--;
    }
    w = (uint32_t *)(void *)p;
    nw = n / 4u;
    for (i = 0; i < nw; ++i)
        w[i] = 0;
    p += nw * 4u;
    n -= nw * 4u;
    while (n--)
        *p++ = 0;
}

static void lang_attach_slot_ram(int i, const ClvmImage *image) {
    uint64_t cap;
    uint64_t want;
    uint8_t *p;
    int prev;
    uint64_t n;
    if (slots[i].proc_id > 0) {
        if (slots[i].user_ram && slots[i].heap_ram) {
            clvm_vm_set_memory(&slots[i].vm, slots[i].heap_ram,
                               slots[i].heap_ram_sz);
            return;
        }
        lang_free_slot_ram(i);
        want = image && image->mem_hint ? (uint64_t)image->mem_hint
                                         : CLVM_SLOT_RAM_DEFAULT;
        if (want < CLVM_MEMORY_SIZE)
            want = CLVM_MEMORY_SIZE;
        want = proc_set_vm(slots[i].proc_id, want);
        if (want == 0)
            return;
        p = proc_vm_ptr(slots[i].proc_id);
        slots[i].heap_ram = p;
        slots[i].heap_ram_sz = want;
        slots[i].user_ram = 1;
        prev = proc_current();
        proc_switch(slots[i].proc_id);
        mem_zero_fast(p, 4096ull);
        proc_switch(prev);
        clvm_vm_set_memory(&slots[i].vm, p, want);
        cls_map_proc(slots[i].proc_id);
        return;
    }

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
    mem_zero_fast(p, want);
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
    slots[slot].fn_n = 0;
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
        unsigned file = 0;
        uint32_t parsed_pc = 0;
        uint16_t parsed_line = 0;
        uint16_t parsed_file = 0;
        while (buf[i] == ' ' || buf[i] == '\n' || buf[i] == '\r')
            i++;
        if (buf[i] == 'F' && buf[i + 1] == ' ') {
            int k = 0;
            int fi;
            i += 2;
            fi = slots[slot].fn_n;
            if (fi < 16) {
                while (buf[i] && buf[i] != ' ' && k < 23) {
                    slots[slot].fn_name[fi][k++] = buf[i++];
                }
                slots[slot].fn_name[fi][k] = 0;
                while (buf[i] == ' ')
                    i++;
                {
                    unsigned pc = 0;
                    while (buf[i] >= '0' && buf[i] <= '9') {
                        pc = pc * 10u + (unsigned)(buf[i] - '0');
                        i++;
                    }
                    slots[slot].fn_pc[fi] = pc;
                    slots[slot].fn_n++;
                }
            }
            while (buf[i] && buf[i] != '\n')
                i++;
            if (buf[i] == '\n')
                i++;
            continue;
        }
        if (!cdbg_parse_map_line(buf + i, &parsed_pc, &parsed_line,
                                 &parsed_file)) {
            while (buf[i] && buf[i] != '\n')
                i++;
            if (buf[i] == '\n')
                i++;
            continue;
        }
        pc = parsed_pc;
        line = parsed_line;
        file = parsed_file;
        slots[slot].map_pc[slots[slot].map_n] = pc;
        slots[slot].map_line[slots[slot].map_n] = (uint16_t)line;
        slots[slot].map_file[slots[slot].map_n] = (uint16_t)file;
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
    if (slots[i].front_pixels) {
        kfree(slots[i].front_pixels);
        slots[i].front_pixels = 0;
        slots[i].front_w = 0;
        slots[i].front_h = 0;
    }
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
    int game3d;
    (void)image;
    lang_slot_release_gfx(i);
    w = CLVM_SYS_GAME_W;
    h = CLVM_SYS_GAME_H;
    game3d = name && name[0] == 'G' && name[1] == 'A' && name[2] == 'M' &&
             name[3] == 'E' && name[4] == 'S' && name[5] == '/';
    if (game3d) {
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
    /* Desktop/taskbar icons: reuse the live instance (stops spawn storms). */
    if (lang_raise_existing(name)) {
        status(e, "already running");
        return 1;
    }
    for (i = 0; i < LANG_VM_SLOTS && slots[i].used; ++i) {
    }
    if (i == LANG_VM_SLOTS) {
        status(e, "run: all VM slots busy");
        serial_puts("run: all VM slots busy\n");
        return 0;
    }
    if (!slot_ensure_file(i)) {
        status(e, "run: no memory");
        serial_puts("run: no memory for slot file\n");
        return 0;
    }
    serial_puts("run: load ");
    serial_puts(name ? name : "?");
    serial_puts("\n");
    n = fs_read(name, slots[i].file, (int)slots[i].file_cap);
    if (n < 0) {
        int k = 0;
        e->status[0] = 0;
        append(e->status, 80, &k, "run: not found ");
        append(e->status, 80, &k, name ? name : "");
        serial_puts("run: not found\n");
        return 0;
    }
    serial_puts("run: bytes=");
    serial_write_u64((uint64_t)(uint32_t)n);
    serial_puts("\n");
    if (storage_ready() && storage_cfs()) {
        if (cfs_perm(storage_cfs(), name, CFS_PERM_EXEC) != CFS_OK) {
            status(e, "no exec");
            serial_puts("run: no exec\n");
            return 0;
        }
    }
    slots[i].file_size = (size_t)n;
    load = clvm_parse(slots[i].file, slots[i].file_size, &image);
    if (load != CL_LOAD_OK) {
        status(e, clvm_load_error(load));
        serial_puts("run: clvm_parse failed\n");
        return 0;
    }
    serial_puts("run: mem_hint=");
    serial_write_u64((uint64_t)image.mem_hint);
    serial_puts("\n");
    if (!lang_setup_viewport(i, &image, name)) {
        status(e, "run: gfx slot failed");
        serial_puts("run: gfx slot failed\n");
        return 0;
    }
    gfx2d_clear(slots[i].gfx.pixels, slots[i].gfx.w, slots[i].gfx.h, 0);
    clvm_vm_init(&slots[i].vm, &image, system_fn, &slots[i].gfx);
    slots[i].vm.on_safepoint = lang_safepoint;
    slots[i].proc_id = proc_create(name);
    if (slots[i].proc_id < 0)
        slots[i].proc_id = 0;
    lang_map_surface(i);
    lang_attach_slot_ram(i, &image);
    if (!slots[i].heap_ram) {
        if (slots[i].proc_id > 0)
            proc_destroy(slots[i].proc_id);
        slots[i].proc_id = 0;
        status(e, "run: heap ram failed");
        serial_puts("run: heap ram failed\n");
        return 0;
    }
    serial_puts("run: heap_ram=");
    serial_write_u64(slots[i].heap_ram_sz);
    serial_puts("\n");
    lang_load_map(i, name);
    slots[i].use_jit = 0;
    slots[i].jit_fn = NULL;
    if (slots[i].jit.phys != 0) {
        jit_free(&slots[i].jit);
    }
    /*
     * UI CLVs (Editor/Shell) need JIT; Doom-sized games already did.
     * Debugger keeps the interpreter.
     */
    if (!g_want_debug) {
        use_jit = 1;
    }
    if (use_jit && !g_want_debug) {
        serial_puts("run: jit compile...\n");
        if (jit_compile_image(&image, &slots[i].jit, &slots[i].jit_fn) != 0) {
            status(e, "jit compile failed");
            serial_puts("run: jit compile failed, using interpreter\n");
            slots[i].use_jit = 0;
            slots[i].jit_fn = NULL;
        } else {
            slots[i].use_jit = 1;
            serial_puts("run: jit ready\n");
        }
    }
    scopy(slots[i].name, LANG_NAME_MAX, name);
    slots[i].caps = path_is_driver(name) ? CAP_DRIVER : 0;
    slots[i].task_id = -1;
    slots[i].used = 1;
    slots[i].debug_on = g_want_debug;
    slots[i].paused = g_want_debug;
    slots[i].step_one = 0;
    slots[i].nbreak = 0;
    slots[i].step_line = 0;
    slots[i].step_mode = DBG_STEP_NONE;
    slots[i].step_depth = 0;
    slots[i].last_line = lang_line_at(&slots[i], slots[i].vm.pc);
    slot_ev_reset(i);
    slots[i].dying = 0;
    app_window_open(i, name);
    status(e, slots[i].use_jit ? "running jit" : "running");
    serial_puts(slots[i].use_jit ? "run: started jit\n" : "run: started interp\n");
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
    return lang_run_path_arg(name, 0);
}

int lang_run_path_replace(const char *name) {
    (void)lang_kill_by_name(name);
    return lang_run_path_arg(name, 0);
}

int lang_checkpoint_save(int slot, const uint8_t *bytes, int n, uint32_t off) {
    int i;
    if (slot < 0 || slot >= LANG_VM_SLOTS || !slots[slot].used || !bytes)
        return -1;
    if (n < 0)
        n = 0;
    if (n > (int)sizeof(slots[slot].checkpoint))
        n = (int)sizeof(slots[slot].checkpoint);
    for (i = 0; i < n; ++i)
        slots[slot].checkpoint[i] = bytes[i];
    slots[slot].checkpoint_n = n;
    slots[slot].checkpoint_off = off;
    return n;
}

int lang_hot_reload(const char *name) {
    int i;
    int n;
    int k;
    ClvmImage image;
    ClvmLoadError load;
    uint8_t *born;
    if (!name || !name[0])
        return 0;
    for (i = 0; i < LANG_VM_SLOTS; ++i) {
        if (slots[i].used && samestr(slots[i].name, name))
            break;
    }
    if (i == LANG_VM_SLOTS)
        return 0;
    if (!slot_ensure_file(i))
        return 0;
    n = fs_read(name, file_buffer, (int)sizeof(file_buffer));
    if (n < 0)
        return 0;
    load = clvm_parse(file_buffer, (size_t)n, &image);
    if (load != CL_LOAD_OK)
        return 0;
    for (k = 0; k < n; ++k)
        slots[i].file[k] = file_buffer[k];
    slots[i].file_size = (size_t)n;
    image.code = slots[i].file + (image.code - file_buffer);
    clvm_vm_init(&slots[i].vm, &image, system_fn, &slots[i].gfx);
    born = slots[i].vm.mem_owned ? slots[i].vm.memory : 0;
    slots[i].vm.on_safepoint = lang_safepoint;
    lang_attach_slot_ram(i, &image);
    if (born && born != slots[i].heap_ram && !slots[i].user_ram)
        kfree(born);
    if (slots[i].checkpoint_n > 0 && slots[i].vm.memory &&
        (uint64_t)slots[i].checkpoint_off + (uint64_t)slots[i].checkpoint_n <=
            slots[i].vm.mem_size) {
        int prev = proc_current();
        if (slots[i].user_ram)
            proc_switch(slots[i].proc_id);
        for (k = 0; k < slots[i].checkpoint_n; ++k)
            slots[i].vm.memory[slots[i].checkpoint_off + (uint32_t)k] =
                slots[i].checkpoint[k];
        proc_switch(prev);
    }
    lang_load_map(i, name);
    serial_puts("run: hot reload ");
    serial_puts(name);
    serial_puts("\n");
    return 1;
}

void lang_set_app_arg(const char *arg) {
    int i = 0;
    if (!arg) {
        g_app_arg[0] = 0;
        return;
    }
    while (arg[i] && i + 1 < (int)sizeof(g_app_arg)) {
        g_app_arg[i] = arg[i];
        i++;
    }
    g_app_arg[i] = 0;
}

int lang_copy_app_arg(char *out, int cap) {
    int i = 0;
    if (!out || cap < 1) {
        return 0;
    }
    while (g_app_arg[i] && i + 1 < cap) {
        out[i] = g_app_arg[i];
        i++;
    }
    out[i] = 0;
    return i > 0;
}

int lang_run_path_arg(const char *name, const char *arg) {
    static Editor dummy;
    char probe[1];
    char alt[LANG_NAME_MAX];

    dummy.name[0] = 0;
    dummy.status[0] = 0;
    lang_set_app_arg(arg);
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

static int lang_disk_cc(const char *src, const char *dst) {
    static uint8_t img[262144];
    static ClvmVm vm;
    char saved[FS_PATH];
    char arg[FS_PATH];
    uint8_t seed[16];
    int n;
    int pid;
    int prev;
    int steps;
    int i;
    int k;
    uint32_t sz = 0;
    uint16_t ty = 0;
    uint64_t bytes;
    ClvmImage image;
    ClvmStepResult r;
    uint8_t *zp;

    n = fs_read("APPS/CC/CC.CLV", img, (int)sizeof(img));
    if (n < 16) {
        serial_puts("cc: no image\n");
        return 0;
    }
    if (clvm_parse(img, (size_t)n, &image) != CL_LOAD_OK) {
        serial_puts("cc: bad image\n");
        return 0;
    }
    if (fs_stat(dst, &sz, &ty) != 0) {
        for (i = 0; i < 16; ++i)
            seed[i] = 0;
        if (fs_write(dst, seed, 16) < 0)
            return 0;
    }
    k = 0;
    while (src[k] && k + 2 < (int)sizeof(arg)) {
        arg[k] = src[k];
        k++;
    }
    arg[k++] = ' ';
    i = 0;
    while (dst[i] && k + 1 < (int)sizeof(arg)) {
        arg[k++] = dst[i++];
    }
    arg[k] = 0;
    i = 0;
    while (g_app_arg[i] && i + 1 < (int)sizeof(saved)) {
        saved[i] = g_app_arg[i];
        i++;
    }
    saved[i] = 0;
    lang_set_app_arg(arg);
    pid = proc_create("cc");
    if (pid <= 0) {
        serial_puts("cc: no proc\n");
        lang_set_app_arg(saved);
        return 0;
    }
    bytes = proc_set_vm(pid, 16ull * 1024ull * 1024ull);
    if (bytes == 0) {
        serial_puts("cc: no vm\n");
        proc_destroy(pid);
        lang_set_app_arg(saved);
        return 0;
    }
    {
        uint64_t off;
        uint64_t cover = 128ull * 4096ull;
        if (cover > bytes)
            cover = bytes;
        for (off = 0; off < cover; off += 4096ull) {
            if (proc_commit(pid, PROC_VM_VIRT + off) != 0) {
                serial_puts("cc: commit\n");
                proc_destroy(pid);
                lang_set_app_arg(saved);
                return 0;
            }
        }
    }
    prev = proc_current();
    proc_switch(pid);
    zp = (uint8_t *)&vm;
    for (i = 0; i < (int)sizeof(vm); ++i)
        zp[i] = 0;
    {
        static uint32_t cc_pix[4];
        static ClvmGfxCtx cc_gfx;
        cc_gfx.pixels = cc_pix;
        cc_gfx.zbuf = 0;
        cc_gfx.w = 2;
        cc_gfx.h = 2;
        cc_gfx.slot_id = -1;
        clvm_vm_init(&vm, &image, clvm_sys_dispatch, &cc_gfx);
    }
    clvm_vm_set_memory(&vm, proc_vm_ptr(pid), bytes);
    r = CLVM_STEP_SLICE;
    for (steps = 0; steps < 20000 && r == CLVM_STEP_SLICE; ++steps) {
        r = clvm_step(&vm, 200000u);
        if ((steps & 15) == 0)
            lang_cc_pump();
    }
    proc_switch(prev);
    proc_destroy(pid);
    lang_set_app_arg(saved);
    if (r != CLVM_STEP_HALT) {
        serial_puts("cc: run ");
        serial_write_u64((uint64_t)r);
        serial_puts(" fault ");
        serial_puts(clvm_fault_text(vm.fault));
        serial_puts(" pc ");
        serial_write_u64(vm.pc);
        serial_puts(" steps ");
        serial_write_u64((uint64_t)steps);
        serial_puts("\n");
        return 0;
    }
    n = fs_read(dst, file_buffer, (int)sizeof(file_buffer));
    if (n < 16 || clvm_parse(file_buffer, (size_t)n, &image) != CL_LOAD_OK) {
        serial_puts("cc: bad out ");
        serial_write_u64((uint64_t)(n < 0 ? 0 : n));
        serial_puts("\n");
        return 0;
    }
    serial_puts("cc: disk compiler ");
    serial_puts(dst);
    serial_puts("\n");
    return 1;
}

void lang_make_cc(void) {
    uint32_t sz = 0;
    uint16_t ty = 0;
    if (fs_stat("APPS/CC/DOCC", &sz, &ty) != 0)
        return;
    if (lang_disk_cc("APPS/CC/STRUCT.CC", "APPS/CC/STRUCT.CLV"))
        serial_puts("guest struct ok\n");
    else
        serial_puts("guest struct fail\n");
    if (lang_disk_cc("SYS/DRV/VIRTIOGPU.CC", "SYS/DRV/VGPU.CLV"))
        serial_puts("guest driver ok\n");
    else
        serial_puts("guest driver fail\n");
    if (lang_disk_cc("APPS/CC/CC.CC", "APPS/CC/CC2.CLV"))
        serial_puts("make cc guest ok\n");
    else
        serial_puts("make cc guest fail\n");
}

static int lang_join_cc(const char **paths, int npaths, const char *dst) {
    int off = 0;
    int i;
    for (i = 0; i < npaths; ++i) {
        int n;
        if (off + 2 >= (int)sizeof(source_buffer))
            return 0;
        n = fs_read(paths[i], source_buffer + off,
                    (int)sizeof(source_buffer) - off - 1);
        if (n < 0)
            return 0;
        off += n;
        source_buffer[off++] = '\n';
        source_buffer[off] = 0;
    }
    if (fs_write("APPS/CC/JOIN.CC", source_buffer, off) < 0)
        return 0;
    return lang_disk_cc("APPS/CC/JOIN.CC", dst);
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
    if (suffix(src_path, ".CC") && lang_disk_cc(src_path, clv_path))
        return 1;
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
    int ok;
    clear_last_lang();
    if (!paths || npaths < 1)
        return 0;
    if (!output_name(paths[0], outn))
        return 0;
    g_cc_hb_tick = (uint32_t)ticks;
    chrisc_set_yield(lang_cc_pump);
    ok = chrisc_compile_files_ex(paths, npaths, chrisc_fs_read, 0, code_buffer,
                                 sizeof(code_buffer), &g_chris_result,
                                 chrisc_serial_progress, 0);
    chrisc_set_yield(0);
    if (!ok) {
        set_err_diag(&g_chris_result.diag);
        serial_puts("cc: ");
        serial_puts(g_last_err[0] ? g_last_err : "compile failed");
        serial_puts("\n");
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
    {
        int only_cc = 1;
        int pi;
        char outn[LANG_NAME_MAX];
        for (pi = 0; pi < npaths; ++pi) {
            if (!suffix(paths[pi], ".CC"))
                only_cc = 0;
        }
        if (only_cc && output_name(lst_path, outn) &&
            lang_join_cc(pp, npaths, outn))
            return 1;
    }
    clear_last_lang();
    g_cc_hb_tick = (uint32_t)ticks;
    chrisc_set_yield(lang_cc_pump);
    {
        int ok = chrisc_compile_files_ex(pp, npaths, chrisc_fs_read, 0, code_buffer,
                                         sizeof(code_buffer), &g_chris_result,
                                         chrisc_serial_progress, 0);
        chrisc_set_yield(0);
        if (!ok) {
            set_err_diag(&g_chris_result.diag);
            serial_puts("cc: ");
            serial_puts(g_last_err[0] ? g_last_err : "compile failed");
            serial_puts("\n");
            return 0;
        }
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
    slots[i].proc_id = proc_create(name);
    if (slots[i].proc_id < 0)
        slots[i].proc_id = 0;
    lang_map_surface(i);
    lang_attach_slot_ram(i, &image);
    lang_load_map(i, name);
    slots[i].use_jit = 0;
    slots[i].jit_fn = NULL;
    if (slots[i].jit.phys != 0) {
        jit_free(&slots[i].jit);
    }
    scopy(slots[i].name, LANG_NAME_MAX, name);
    slots[i].caps = path_is_driver(name) ? CAP_DRIVER : 0;
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
    int n;
    int i;
    static int rr;

    bench_frame_tick();
    clvm_threads_tick();
    for (n = 0; n < LANG_VM_SLOTS; ++n) {
        i = (rr + n) % LANG_VM_SLOTS;
        if (slots[i].used) {
            ClvmStepResult r;
            int b;
            int game;
            int slices;
            int max_slices;
            int entered;
            uint32_t budget;
            if (slots[i].paused && !slots[i].step_one && !slots[i].step_line)
                continue;
            if (slots[i].proc_id > 0 && !proc_runnable(slots[i].proc_id))
                continue;
            clvm_vm_wake(&slots[i].vm, now);
            if (slots[i].debug_on) {
                for (b = 0; b < slots[i].nbreak; ++b) {
                    if (slots[i].breakpoints[b] == slots[i].vm.pc) {
                        slots[i].paused = 1;
                        slots[i].step_one = 0;
                        slots[i].step_line = 0;
                        slots[i].step_mode = DBG_STEP_NONE;
                        break;
                    }
                }
                if (slots[i].paused && !slots[i].step_one && !slots[i].step_line)
                    continue;
            }
            entered = 0;
            if (slots[i].proc_id > 0 && proc_alive(slots[i].proc_id)) {
                proc_switch(slots[i].proc_id);
                entered = 1;
            }
            game = slots[i].heap_ram_sz >= (16ull * 1024ull * 1024ull);
            if (slots[i].debug_on && slots[i].step_line) {
                int k;
                int mode = slots[i].step_mode;
                int limit;
                uint16_t start = slots[i].last_line;
                int depth0 = slots[i].step_depth;
                if (mode == DBG_STEP_NONE)
                    mode = DBG_STEP_IN;
                limit = mode == DBG_STEP_IN ? 512 : 8192;
                r = CLVM_STEP_SLICE;
                for (k = 0; k < limit; ++k) {
                    r = clvm_step(&slots[i].vm, 1);
                    if (r == CLVM_STEP_HALT || r == CLVM_STEP_FAULT)
                        break;
                    if (dbg_step_should_pause(
                            mode, (int)start, depth0,
                            (int)lang_line_at(&slots[i], slots[i].vm.pc),
                            (int)slots[i].vm.csp))
                        break;
                }
                slots[i].step_line = 0;
                slots[i].step_mode = DBG_STEP_NONE;
                slots[i].paused = 1;
                slots[i].last_line = lang_line_at(&slots[i], slots[i].vm.pc);
            } else if (slots[i].debug_on && slots[i].step_one) {
                r = clvm_step(&slots[i].vm, 1);
                slots[i].step_one = 0;
                slots[i].paused = 1;
            } else {
                if (game) {
                    budget = LANG_VM_BUDGET_GAME;
                    max_slices = 1;
                } else {
                    budget = LANG_VM_BUDGET_UI;
                    max_slices = 8;
                }
                r = CLVM_STEP_SLICE;
                for (slices = 0; slices < max_slices; slices++) {
                    if (slots[i].use_jit && slots[i].jit_fn != NULL &&
                        !slots[i].debug_on) {
                        jit_set_sys_context(&slots[i].vm, &slots[i].gfx);
                        r = slots[i].jit_fn(&slots[i].vm, budget, now);
                    } else {
                        r = clvm_step(&slots[i].vm, budget);
                    }
                    if (r != CLVM_STEP_SLICE) {
                        break;
                    }
                    if (proc_slice_due()) {
                        proc_slice_ack();
                        break;
                    }
                    if (game) {
                        break;
                    }
                    if ((uint32_t)ticks - now >= 8u) {
                        break;
                    }
                    clvm_vm_wake(&slots[i].vm, now);
                }
            }
            if (entered)
                proc_switch(PROC_KERNEL);
    if (r == CLVM_STEP_HALT || r == CLVM_STEP_FAULT || slots[i].dying) {
                if (r == CLVM_STEP_FAULT) {
                    proc_record_fault(slots[i].proc_id, 0, 0,
                                      (uint64_t)slots[i].vm.pc);
                    serial_puts("run: FAULT slot=");
                    serial_write_u64((uint64_t)(uint32_t)i);
                    serial_puts(" pc=");
                    serial_write_u64((uint64_t)slots[i].vm.pc);
                    serial_puts(" line=");
                    serial_write_u64((uint64_t)lang_line_at(&slots[i],
                                                            slots[i].vm.pc));
                    serial_puts(" sp=");
                    serial_write_u64((uint64_t)slots[i].vm.sp);
                    serial_puts(" fault=");
                    serial_write_u64((uint64_t)(uint32_t)slots[i].vm.fault);
                    serial_puts(" fpc=");
                    serial_write_u64((uint64_t)slots[i].vm.fault_pc);
                    serial_puts(" csp=");
                    serial_write_u64((uint64_t)slots[i].vm.csp);
                    if (slots[i].vm.csp > 0u) {
                        uint16_t ci;
                        serial_puts(" ret=");
                        serial_write_u64(
                            (uint64_t)slots[i].vm.calls[slots[i].vm.csp - 1u]);
                        serial_puts(" calls=");
                        for (ci = 0; ci < slots[i].vm.csp && ci < 12u; ci++) {
                            if (ci)
                                serial_puts(",");
                            serial_write_u64((uint64_t)slots[i].vm.calls[ci]);
                        }
                    }
                    if (slots[i].vm.sp > 0u) {
                        serial_puts(" top=");
                        serial_write_u64(
                            (uint64_t)slots[i].vm.stack[slots[i].vm.sp - 1u]);
                    }
                    serial_puts("\n");
                    /* Doom-sized: never drop to interpreter (too slow / state
                     * already partial). Kill on unexpected fault. */
                    if (slots[i].use_jit &&
                        slots[i].heap_ram_sz >= (16ull * 1024ull * 1024ull)) {
                        serial_puts("run: doom fault — kill\n");
                    }
                }
                lang_kill(i);
            }
        }
    }
    rr = (rr + 1) % LANG_VM_SLOTS;
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
    if (slots[slot].proc_id > 0) {
        proc_destroy(slots[slot].proc_id);
        slots[slot].proc_id = 0;
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

uint32_t lang_slot_caps(int slot) {
    if (slot < 0 || slot >= LANG_VM_SLOTS || !slots[slot].used)
        return 0;
    return slots[slot].caps;
}

static int path_is_driver(const char *name) {
    int i;
    if (!name)
        return 0;
    for (i = 0; name[i]; ++i) {
        if (name[i] == 'S' && name[i + 1] == 'Y' && name[i + 2] == 'S' &&
            name[i + 3] == '/' && name[i + 4] == 'D' && name[i + 5] == 'R' &&
            name[i + 6] == 'V')
            return 1;
    }
    return 0;
}

uint32_t *lang_slot_pixels(int slot) {
    if (slot < 0 || slot >= LANG_VM_SLOTS) {
        return 0;
    }
    if (slots[slot].front_pixels && slots[slot].front_w == slots[slot].gfx.w &&
        slots[slot].front_h == slots[slot].gfx.h) {
        return slots[slot].front_pixels;
    }
    return slots[slot].gfx.pixels;
}

void lang_slot_publish(int slot) {
    int n;
    uint32_t *src;
    if (slot < 0 || slot >= LANG_VM_SLOTS || !slots[slot].used) {
        return;
    }
    src = slots[slot].gfx.pixels;
    if (!src || slots[slot].gfx.w < 1 || slots[slot].gfx.h < 1) {
        return;
    }
    n = slots[slot].gfx.w * slots[slot].gfx.h;
    if (!slots[slot].front_pixels || slots[slot].front_w != slots[slot].gfx.w ||
        slots[slot].front_h != slots[slot].gfx.h) {
        if (slots[slot].front_pixels) {
            kfree(slots[slot].front_pixels);
            slots[slot].front_pixels = 0;
        }
        slots[slot].front_pixels =
            (uint32_t *)kmalloc((uint64_t)n * sizeof(uint32_t));
        if (!slots[slot].front_pixels) {
            slots[slot].front_w = 0;
            slots[slot].front_h = 0;
            return;
        }
        slots[slot].front_w = slots[slot].gfx.w;
        slots[slot].front_h = slots[slot].gfx.h;
    }
    gfx_fast_copy_u32(slots[slot].front_pixels, src, n);
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
        tmp[t++] = ' ';
        {
            unsigned x = r->map[i].file_id;
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
    for (i = 0; i < r->nexports && n + 40 < (int)sizeof(buf); ++i) {
        int c;
        buf[n++] = 'F';
        buf[n++] = ' ';
        for (c = 0; r->export_name[i][c] && n + 8 < (int)sizeof(buf); ++c)
            buf[n++] = r->export_name[i][c];
        buf[n++] = ' ';
        {
            unsigned x = r->export_pc[i];
            char dec[12];
            int d = 0;
            if (x == 0)
                dec[d++] = '0';
            while (x && d < 12) {
                dec[d++] = (char)('0' + (x % 10));
                x /= 10;
            }
            while (d && n + 2 < (int)sizeof(buf))
                buf[n++] = dec[--d];
        }
        buf[n++] = '\n';
    }
    buf[n] = 0;
    if (n > 0)
        (void)fs_write(mapn, buf, n);
    {
        char dbgn[LANG_NAME_MAX];
        int cap = cdbg_bound(r);
        uint8_t *blob;
        int wrote;
        clv_to_cdbg(clv_path, dbgn);
        if (dbgn[0] && cap > 0) {
            blob = (uint8_t *)kmalloc((size_t)cap);
            if (blob) {
                wrote = cdbg_from_result(blob, cap, r, 0, 0, 0);
                if (wrote > 0)
                    (void)fs_write(dbgn, blob, wrote);
                kfree(blob);
            }
        }
    }
}

void lang_debug_enable(int on) {
    g_want_debug = on;
}

static void lang_begin_step(int mode) {
    int i;
    for (i = 0; i < LANG_VM_SLOTS; ++i) {
        if (slots[i].used && slots[i].debug_on) {
            slots[i].last_line = lang_line_at(&slots[i], slots[i].vm.pc);
            slots[i].step_mode = mode;
            slots[i].step_depth = (int)slots[i].vm.csp;
            slots[i].step_line = 1;
            slots[i].step_one = 0;
            slots[i].paused = 0;
        }
    }
}

void lang_debug_step(void) {
    lang_begin_step(DBG_STEP_IN);
}

void lang_debug_step_over(void) {
    lang_begin_step(DBG_STEP_OVER);
}

void lang_debug_step_out(void) {
    lang_begin_step(DBG_STEP_OUT);
}

void lang_debug_continue(void) {
    int i;
    for (i = 0; i < LANG_VM_SLOTS; ++i) {
        if (slots[i].used && slots[i].debug_on) {
            slots[i].paused = 0;
            slots[i].step_one = 0;
            slots[i].step_line = 0;
            slots[i].step_mode = DBG_STEP_NONE;
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

uint32_t lang_debug_call(int depth) {
    int s;
    for (s = 0; s < LANG_VM_SLOTS; ++s) {
        if (slots[s].used && slots[s].debug_on) {
            if (depth < 0 || depth >= (int)slots[s].vm.csp)
                return 0;
            return slots[s].vm.calls[slots[s].vm.csp - 1 - depth];
        }
    }
    return 0;
}

const char *lang_debug_fn(uint32_t pc) {
    int s;
    int i;
    int best = -1;
    for (s = 0; s < LANG_VM_SLOTS; ++s) {
        if (!slots[s].used || !slots[s].debug_on)
            continue;
        for (i = 0; i < slots[s].fn_n; ++i) {
            if (slots[s].fn_pc[i] <= pc &&
                (best < 0 || slots[s].fn_pc[i] >= slots[s].fn_pc[best]))
                best = i;
        }
        if (best >= 0)
            return slots[s].fn_name[best];
    }
    return "";
}

void lang_debug_set_watch(uint32_t addr) {
    int s;
    for (s = 0; s < LANG_VM_SLOTS; ++s) {
        if (slots[s].used && slots[s].debug_on)
            slots[s].watch = addr;
    }
}

uint32_t lang_debug_watch(void) {
    int s;
    for (s = 0; s < LANG_VM_SLOTS; ++s) {
        if (slots[s].used && slots[s].debug_on)
            return slots[s].watch;
    }
    return 0;
}

int lang_debug_sys(int index, int *id) {
    int slot = -1;
    int sys = 0;
    clvm_sys_trace(index, &sys, &slot);
    if (id)
        *id = sys;
    return slot;
}

int lang_debug_fault(uint64_t *cr2, int *pid, uint64_t *rip) {
    const ProcFault *f = proc_last_fault();
    if (!f || !f->valid)
        return 0;
    if (cr2)
        *cr2 = f->cr2;
    if (pid)
        *pid = f->pid;
    if (rip)
        *rip = f->rip;
    return 1;
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
            int prev = proc_current();
            int32_t v;
            const uint8_t *m;
            if (slots[s].user_ram)
                proc_switch(slots[s].proc_id);
            m = slots[s].vm.memory;
            if (!m || (uint64_t)addr + 4u > slots[s].vm.mem_size) {
                proc_switch(prev);
                return 0;
            }
            v = (int32_t)((uint32_t)m[addr] | ((uint32_t)m[addr + 1] << 8) |
                          ((uint32_t)m[addr + 2] << 16) |
                          ((uint32_t)m[addr + 3] << 24));
            proc_switch(prev);
            return v;
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
