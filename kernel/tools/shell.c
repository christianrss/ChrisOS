#include "shell.h"
#include "cfs.h"
#include "editor.h"
#include "editor_window.h"
#include "explorer.h"
#include "fs.h"
#include "graphics.h"
#include "input.h"
#include "lang_pipeline.h"
#include "task.h"
#include "taskmgr.h"
#include "ui.h"
#include "pit.h"
#include "net.h"
#include "elf.h"
#include "proc.h"
#include "user_enter.h"
#include "chrisbuild.h"
#include "chrismake.h"
#include "sock.h"
#include "port.h"
#include "heap.h"
#include "kcc.h"
#include "chrisasm.h"
#include "chrisld.h"
#include "chriso.h"
#include "native_link.h"
#include "bench.h"

#define SH_COLS 48
#define SH_ROWS 24
#define SH_HIST 8
#define SH_LINE 512

static int g_cmd_ok;

static char g_lines[SH_ROWS][SH_COLS];
static int g_nlines;
static char g_in[SH_LINE];
static int g_inlen;
static char g_hist[SH_HIST][SH_LINE];
static int g_nhist;
static int g_hcur;
static char g_cwd[512];
static char g_out[SH_LINE];
static Editor g_sh_ed;

static void sh_copy(char *d, int cap, const char *s) {
    int i = 0;
    if (cap < 1) return;
    if (!s) { d[0] = 0; return; }
    while (s[i] && i < cap - 1) { d[i] = s[i]; i++; }
    d[i] = 0;
}

static char sh_lower(char c) {
    if (c >= 'A' && c <= 'Z') {
        return (char)(c + ('a' - 'A'));
    }
    return c;
}

static int sh_eq(const char *a, const char *b) {
    int i = 0;
    while (a[i] && b[i] && sh_lower(a[i]) == sh_lower(b[i])) {
        i++;
    }
    return a[i] == 0 && b[i] == 0;
}

static int sh_blank(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static void sh_emit(const char *s) {
    int i;
    if (g_nlines == SH_ROWS) {
        for (i = 0; i < SH_ROWS - 1; i++)
            sh_copy(g_lines[i], SH_COLS, g_lines[i + 1]);
        g_nlines = SH_ROWS - 1;
    }
    sh_copy(g_lines[g_nlines], SH_COLS, s);
    g_nlines++;
}

static void sh_emit_prefixed(const char *pfx, const char *path);
static void sh_exec(const char *line);

static void join_cwd(const char *name, char *out, int cap) {
    if (!name || !name[0] || (name[0] == '/' && !name[1])) {
        out[0] = 0;
        return;
    }
    if (name[0] == '/') {
        sh_copy(out, cap, name + 1);
        return;
    }
    if (!g_cwd[0]) {
        sh_copy(out, cap, name);
        return;
    }
    {
        int n = 0;
        while (g_cwd[n]) n++;
        sh_copy(out, cap, g_cwd);
        if (n + 2 < cap) {
            out[n] = '/';
            sh_copy(out + n + 1, cap - n - 1, name);
        }
    }
}

static int tok1(const char *line, char *cmd, int ccap, char *arg, int acap) {
    int i = 0;
    int j = 0;
    while (line[i] && sh_blank(line[i])) {
        i++;
    }
    while (line[i] && !sh_blank(line[i]) && j < ccap - 1) {
        cmd[j++] = sh_lower(line[i++]);
    }
    cmd[j] = 0;
    while (line[i] && sh_blank(line[i])) {
        i++;
    }
    j = 0;
    while (line[i] && j < acap - 1) {
        arg[j++] = line[i++];
    }
    while (j > 0 && sh_blank(arg[j - 1])) {
        arg[--j] = 0;
    }
    arg[j] = 0;
    return cmd[0] != 0;
}

static int ls_cb(void *ctx, const char *name, uint32_t size, uint16_t type) {
    char line[SH_COLS];
    int n = 0;
    (void)ctx;
    line[n++] = (type == CFS_INODE_DIR) ? 'd' : 'f';
    line[n++] = ' ';
    while (name[n - 2] && n < SH_COLS - 1) {
        line[n] = name[n - 2];
        n++;
    }
    (void)size;
    line[n] = 0;
    sh_emit(line);
    return 0;
}

static void cmd_help(void) {
    sh_emit("help ls cd pwd cat mkdir rmdir");
    sh_emit("rm mv ed cc kcc as mk make");
    sh_emit("run jit runelf ps kill clear");
    sh_emit("cc .CC/.LST jit .CVA run .CLV");
    sh_emit("make -f Makefile  as .S kcc .C");
    sh_emit("ticks net bench reboot");
}

static void cmd_ls(void) {
    int rc = fs_list_at(g_cwd, ls_cb, 0);
    if (rc < 0) sh_emit("ls failed");
}

static void cmd_pwd(void) {
    sh_emit(g_cwd[0] ? g_cwd : "/");
}

static void cmd_cd(const char *arg) {
    char path[FS_PATH];
    uint32_t sz;
    uint16_t ty;
    if (sh_eq(arg, "") || sh_eq(arg, "/")) {
        g_cwd[0] = 0;
        return;
    }
    join_cwd(arg, path, FS_PATH);
    if (fs_stat(path, &sz, &ty) != CFS_OK || ty != CFS_INODE_DIR) {
        sh_emit("not a dir");
        return;
    }
    sh_copy(g_cwd, FS_PATH, path);
}

static void cmd_cat(const char *arg) {
    char path[FS_PATH];
    char buf[200];
    int n, i, o = 0;
    join_cwd(arg, path, FS_PATH);
    n = fs_read(path, buf, 199);
    if (n < 0) { sh_emit("cat failed"); return; }
    buf[n] = 0;
    for (i = 0; i <= n; i++) {
        if (i == n || buf[i] == '\n' || o == SH_COLS - 1) {
            g_out[o] = 0;
            sh_emit(g_out);
            o = 0;
        } else {
            g_out[o++] = buf[i];
        }
    }
}

static void cmd_mkdir(const char *arg) {
    char path[FS_PATH];
    join_cwd(arg, path, FS_PATH);
    if (fs_mkdir(path) != CFS_OK) sh_emit("mkdir failed");
}

static void cmd_rmdir(const char *arg) {
    char path[FS_PATH];
    join_cwd(arg, path, FS_PATH);
    if (fs_rmdir(path) != CFS_OK) sh_emit("rmdir failed");
}

static void cmd_rm(const char *arg) {
    char path[FS_PATH];
    join_cwd(arg, path, FS_PATH);
    if (fs_unlink(path) != CFS_OK) sh_emit("rm failed");
}

static void cmd_mv(const char *line) {
    char a[FS_PATH], b[FS_PATH], pa[FS_PATH], pb[FS_PATH];
    int i = 0, j = 0;
    while (line[i] == ' ') i++;
    while (line[i] && line[i] != ' ' && j < FS_PATH - 1) a[j++] = line[i++];
    a[j] = 0;
    while (line[i] == ' ') i++;
    j = 0;
    while (line[i] && j < FS_PATH - 1) b[j++] = line[i++];
    b[j] = 0;
    join_cwd(a, pa, FS_PATH);
    join_cwd(b, pb, FS_PATH);
    if (fs_rename(pa, pb) != CFS_OK) sh_emit("mv failed");
}

static void cmd_ed(const char *arg) {
    char path[FS_PATH];
    join_cwd(arg, path, FS_PATH);
    editor_window_open_path(path);
}

static int path_is_lst(const char *path) {
    int a = 0;
    while (path[a]) {
        a++;
    }
    if (a < 4) {
        return 0;
    }
    return sh_lower(path[a - 4]) == '.' && sh_lower(path[a - 3]) == 'l' &&
           sh_lower(path[a - 2]) == 's' && sh_lower(path[a - 1]) == 't';
}

static void cc_fail(const char *fallback) {
    const char *e = lang_last_error();
    g_cmd_ok = 0;
    sh_emit(e && e[0] ? e : fallback);
}

static void cc_ok(int run_after) {
    const char *clv = lang_last_clv();
    if (!clv || !clv[0]) {
        sh_emit("compiled");
        return;
    }
    sh_emit_prefixed("compiled ", clv);
    if (lang_hot_reload(clv)) {
        sh_emit("reloaded");
    }
    if (!run_after) {
        return;
    }
    ed_init(&g_sh_ed);
    ed_set_name(&g_sh_ed, clv);
    if (!lang_run(&g_sh_ed, clv)) {
        sh_emit(g_sh_ed.status);
        g_cmd_ok = 0;
    }
}

static void cmd_cc(const char *arg) {
    static char raw[32][FS_PATH];
    static char path[32][FS_PATH];
    const char *pp[32];
    int n = 0;
    int i = 0;
    int j;
    int run_after = 1;
    while (arg[i] && n < 32) {
        j = 0;
        while (arg[i] == ' ')
            i++;
        if (!arg[i])
            break;
        while (arg[i] && arg[i] != ' ' && j < FS_PATH - 1)
            raw[n][j++] = arg[i++];
        raw[n][j] = 0;
        if (n == 0 && raw[n][0] == '-' && raw[n][1] == 'c' &&
            raw[n][2] == 0) {
            run_after = 0;
            continue;
        }
        join_cwd(raw[n], path[n], FS_PATH);
        pp[n] = path[n];
        n++;
    }
    if (n == 0) {
        g_cmd_ok = 0;
        sh_emit("cc: files");
        return;
    }
    if (n == 1 && path_is_lst(path[0])) {
        if (!lang_compile_list(path[0])) {
            cc_fail("cc lst fail");
            return;
        }
        cc_ok(run_after);
        return;
    }
    if (n == 1) {
        ed_init(&g_sh_ed);
        ed_set_name(&g_sh_ed, path[0]);
        if (ed_open(&g_sh_ed) != CFS_OK) {
            g_cmd_ok = 0;
            sh_emit("cc open");
            return;
        }
        if (!lang_compile(&g_sh_ed)) {
            g_cmd_ok = 0;
            sh_emit(g_sh_ed.status);
            return;
        }
        cc_ok(run_after);
        return;
    }
    if (!lang_compile_many(pp, n)) {
        cc_fail("cc fail");
        return;
    }
    cc_ok(run_after);
}

static void cmd_run(const char *arg) {
    char path[FS_PATH];
    join_cwd(arg, path, FS_PATH);
    ed_init(&g_sh_ed);
    ed_set_name(&g_sh_ed, path);
    if (!lang_run(&g_sh_ed, path)) {
        g_cmd_ok = 0;
        sh_emit(g_sh_ed.status);
    }
}

static void cmd_jit(const char *arg) {
    char path[FS_PATH];
    join_cwd(arg, path, FS_PATH);
    ed_init(&g_sh_ed);
    ed_set_name(&g_sh_ed, path);
    if (ed_open(&g_sh_ed) != CFS_OK) {
        g_cmd_ok = 0;
        sh_emit("jit open");
        return;
    }
    if (!lang_compile_run_jit(&g_sh_ed)) {
        g_cmd_ok = 0;
        sh_emit(g_sh_ed.status);
    } else {
        sh_emit("jit running");
    }
}

static void cmd_runelf(const char *arg) {
    char path[FS_PATH];
    uint8_t *buf;
    uint64_t entry;
    int n;

    join_cwd(arg, path, FS_PATH);
    buf = (uint8_t *)kmalloc(CHRISLD_ELF_MAX);
    if (!buf) {
        sh_emit("runelf nomem");
        return;
    }
    n = fs_read(path, buf, (int)CHRISLD_ELF_MAX);
    if (n < 0) {
        sh_emit("runelf open");
        kfree(buf);
        return;
    }
    if (elf_load(buf, (uint32_t)n, &entry) != 0) {
        sh_emit("runelf bad");
        kfree(buf);
        return;
    }
    kfree(buf);
    enter_user(entry, 0x400FF8ull);
    proc_switch(0);
    sh_emit("runelf done");
}

static void cmd_ps(void) {
    int i, n = task_count();
    for (i = 0; i < n; i++) {
        Task *t = task_iter(i);
        char line[SH_COLS];
        int k = 0;
        unsigned id;
        if (!t) continue;
        id = (unsigned)t->id;
        line[k++] = '0' + (char)(id / 10u);
        line[k++] = '0' + (char)(id % 10u);
        line[k++] = ' ';
        {
            const char *s = task_title(t);
            int p = 0;
            while (s[p] && k < SH_COLS - 1) line[k++] = s[p++];
        }
        line[k] = 0;
        sh_emit(line);
    }
}

static void cmd_kill(const char *arg) {
    int id = 0, i = 0;
    while (arg[i] >= '0' && arg[i] <= '9') {
        id = id * 10 + (arg[i] - '0');
        i++;
    }
    {
        Task *t = task_get(id);
        if (!t) { sh_emit("no task"); return; }
        if (t->type == TASK_APP) lang_kill(t->state.app.lang_slot);
        task_close(id);
    }
}

static void cmd_clear(void) {
    int i;
    for (i = 0; i < SH_ROWS; i++) g_lines[i][0] = 0;
    g_nlines = 0;
}

static void cmd_net(void) {
    char line[SH_COLS];
    net_status(line, SH_COLS);
    sh_emit(line);
}

static void sh_emit_prefixed(const char *pfx, const char *path) {
    char line[SH_LINE];
    int k = 0;
    int i = 0;
    while (pfx[i] && k < SH_LINE - 1) {
        line[k++] = pfx[i++];
    }
    i = 0;
    while (path[i] && k < SH_LINE - 1) {
        line[k++] = path[i++];
    }
    line[k] = 0;
    sh_emit(line);
}

static void cmd_kcc(const char *arg) {
    char path[FS_PATH];
    char out_path[FS_PATH];
    char *src_buf;
    ChrisoImage img;
    int n;

    if (!arg[0]) {
        g_cmd_ok = 0;
        sh_emit("kcc: path");
        return;
    }
    src_buf = (char *)kmalloc(65536u);
    if (!src_buf) {
        g_cmd_ok = 0;
        sh_emit("kcc nomem");
        return;
    }
    join_cwd(arg, path, FS_PATH);
    n = fs_read(path, src_buf, 65535);
    if (n < 0) {
        g_cmd_ok = 0;
        sh_emit("kcc open");
        goto done;
    }
    src_buf[n] = 0;
        if (kcc_compile_source(src_buf, &img) != 0) {
            g_cmd_ok = 0;
            sh_emit("kcc fail");
            goto done;
        }
    native_elf_path(path, out_path, FS_PATH);
    if (native_link_write_elf(&img, out_path) < 0) {
        g_cmd_ok = 0;
        sh_emit("kcc link");
        native_image_free(&img);
        goto done;
    }
    native_image_free(&img);
    sh_emit_prefixed("kcc ok ", out_path);

done:
    kfree(src_buf);
}

static int path_ext_asm(const char *path) {
    int n = 0;
    while (path[n]) {
        n++;
    }
    if (n >= 2 && path[n - 2] == '.' && sh_lower(path[n - 1]) == 's') {
        return 1;
    }
    if (n >= 4 && sh_lower(path[n - 4]) == '.' && sh_lower(path[n - 3]) == 'a' &&
        sh_lower(path[n - 2]) == 's' && sh_lower(path[n - 1]) == 'm') {
        return 1;
    }
    return 0;
}

static void cmd_as(const char *arg) {
    char path[FS_PATH];
    char out_path[FS_PATH];
    char *src_buf;
    ChrisoImage img;
    int n;

    if (!arg[0]) {
        sh_emit("as: path");
        return;
    }
    join_cwd(arg, path, FS_PATH);
    if (!path_ext_asm(path)) {
        sh_emit("as: need .S");
        return;
    }
    src_buf = (char *)kmalloc(65536u);
    if (!src_buf) {
        sh_emit("as nomem");
        return;
    }
    n = fs_read(path, src_buf, 65535);
    if (n < 0) {
        sh_emit("as open");
        goto done;
    }
    src_buf[n] = 0;
    if (chrisasm_assemble(src_buf, &img) != 0) {
        sh_emit("as fail");
        goto done;
    }
    native_elf_path(path, out_path, FS_PATH);
    if (native_link_write_elf(&img, out_path) < 0) {
        sh_emit("as link");
        native_image_free(&img);
        goto done;
    }
    native_image_free(&img);
    sh_emit_prefixed("as ok ", out_path);

done:
    kfree(src_buf);
}

static void cmd_mk(const char *arg) {
    if (sh_eq(arg, "kernel")) {
        if (chrisbuild_mk_kernel() == 0) {
            sh_emit("mk kernel ok");
        } else {
            sh_emit("mk kernel fail");
            g_cmd_ok = 0;
        }
        return;
    }
    if (sh_eq(arg, "clean")) {
        chrisbuild_mk_clean();
        sh_emit("mk clean ok");
        return;
    }
    if (sh_eq(arg, "install")) {
        if (chrisbuild_mk_install() == 0) {
            sh_emit("mk install ok");
        } else {
            sh_emit("mk install fail");
        }
        return;
    }
    sh_emit("mk kernel|clean|install");
}

static int make_stamp(void *user, const char *path, uint64_t *mtime) {
    char full[FS_PATH];
    (void)user;
    if (!path || !mtime) {
        return -1;
    }
    join_cwd(path, full, FS_PATH);
    if (fs_mtime(full, mtime) == CFS_OK) {
        return 0;
    }
    if (fs_mtime(path, mtime) == CFS_OK) {
        return 0;
    }
    return -1;
}

static int make_recipe(void *user, const char *recipe, char *err, int err_cap) {
    (void)user;
    g_cmd_ok = 1;
    sh_exec(recipe);
    if (g_cmd_ok) {
        return 1;
    }
    {
        const char *e = lang_last_error();
        if (e && e[0]) {
            sh_copy(err, err_cap, e);
        } else if (g_sh_ed.status[0]) {
            sh_copy(err, err_cap, g_sh_ed.status);
        } else {
            sh_copy(err, err_cap, "make: recipe fail");
        }
    }
    return 0;
}

static void cmd_make(const char *arg) {
    char mkpath[FS_PATH];
    char target[SH_LINE];
    static char text[16384];
    char err[CHRISMAKE_MSG];
    const char *p = arg;
    int n;
    uint32_t sz;
    uint16_t ty;

    mkpath[0] = 0;
    target[0] = 0;
    while (*p) {
        int j = 0;
        char tok[FS_PATH];
        while (*p == ' ' || *p == '\t') {
            p++;
        }
        if (!*p) {
            break;
        }
        if (p[0] == '-' && p[1] == 'f') {
            const char *fp;
            p += 2;
            if (*p == ' ' || *p == '\t' || !*p) {
                while (*p == ' ' || *p == '\t') {
                    p++;
                }
                fp = p;
                j = 0;
                while (*p && *p != ' ' && *p != '\t' && j < FS_PATH - 1) {
                    tok[j++] = *p++;
                }
                tok[j] = 0;
                (void)fp;
            } else {
                j = 0;
                while (*p && *p != ' ' && *p != '\t' && j < FS_PATH - 1) {
                    tok[j++] = *p++;
                }
                tok[j] = 0;
            }
            if (!tok[0]) {
                g_cmd_ok = 0;
                sh_emit("make: -f path");
                return;
            }
            join_cwd(tok, mkpath, FS_PATH);
            continue;
        }
        j = 0;
        while (*p && *p != ' ' && *p != '\t' && j < SH_LINE - 1) {
            target[j++] = *p++;
        }
        target[j] = 0;
    }
    if (!mkpath[0]) {
        join_cwd("Makefile", mkpath, FS_PATH);
        if (fs_stat(mkpath, &sz, &ty) != CFS_OK) {
            join_cwd("makefile", mkpath, FS_PATH);
        }
    }
    n = fs_read(mkpath, text, (int)sizeof(text) - 1);
    if (n < 0) {
        g_cmd_ok = 0;
        sh_emit("make: no Makefile");
        return;
    }
    text[n] = 0;
    if (!chrismake_run_stamped(text, target, make_recipe, make_stamp, 0, err,
                               (int)sizeof(err))) {
        g_cmd_ok = 0;
        sh_emit(err[0] ? err : "make fail");
        return;
    }
    sh_emit("make ok");
}

static void cmd_reboot(void) {
    sh_emit("rebooting");
    machine_reboot();
}

static void cmd_rebuild(void) {
    host_rebuild_start();
    sh_emit("rebuild asked");
}

static void cmd_bench(void) {
    uint32_t fps = bench_fps_estimate();
    char line[24];
    int k = 0;
    unsigned v = fps;
    char d[12];
    int n = 0;
    int i;
    const char *pfx = "fps ";
    for (i = 0; pfx[i]; ++i)
        line[k++] = pfx[i];
    if (v == 0)
        d[n++] = '0';
    while (v && n < 10) {
        d[n++] = (char)('0' + v % 10);
        v /= 10;
    }
    while (n)
        line[k++] = d[--n];
    line[k] = 0;
    sh_emit(line);
}

static void cmd_ticks(void) {
    unsigned t = (unsigned)pit_ticks();
    char line[12];
    int k = 0;
    unsigned v = t;
    char d[12];
    int n = 0;
    if (v == 0) d[n++] = '0';
    while (v && n < 10) { d[n++] = (char)('0' + v % 10); v /= 10; }
    while (n) line[k++] = d[--n];
    line[k] = 0;
    sh_emit(line);
}

static void sh_exec(const char *line) {
    char cmd[16], arg[SH_LINE];
    if (!tok1(line, cmd, 16, arg, SH_LINE)) return;
    g_cmd_ok = 1;
    if (sh_eq(cmd, "help")) cmd_help();
    else if (sh_eq(cmd, "ls")) cmd_ls();
    else if (sh_eq(cmd, "cd")) cmd_cd(arg);
    else if (sh_eq(cmd, "pwd")) cmd_pwd();
    else if (sh_eq(cmd, "cat")) cmd_cat(arg);
    else if (sh_eq(cmd, "mkdir")) cmd_mkdir(arg);
    else if (sh_eq(cmd, "rmdir")) cmd_rmdir(arg);
    else if (sh_eq(cmd, "rm")) cmd_rm(arg);
    else if (sh_eq(cmd, "mv")) cmd_mv(arg);
    else if (sh_eq(cmd, "ed")) cmd_ed(arg);
    else if (sh_eq(cmd, "cc")) cmd_cc(arg);
    else if (sh_eq(cmd, "kcc")) cmd_kcc(arg);
    else if (sh_eq(cmd, "as")) cmd_as(arg);
    else if (sh_eq(cmd, "mk")) cmd_mk(arg);
    else if (sh_eq(cmd, "make")) cmd_make(arg);
    else if (sh_eq(cmd, "rebuild")) cmd_rebuild();
    else if (sh_eq(cmd, "reboot")) cmd_reboot();
    else if (sh_eq(cmd, "run")) cmd_run(arg);
    else if (sh_eq(cmd, "jit")) cmd_jit(arg);
    else if (sh_eq(cmd, "runelf")) cmd_runelf(arg);
    else if (sh_eq(cmd, "ps")) cmd_ps();
    else if (sh_eq(cmd, "kill")) cmd_kill(arg);
    else if (sh_eq(cmd, "clear")) cmd_clear();
    else if (sh_eq(cmd, "ticks")) cmd_ticks();
    else if (sh_eq(cmd, "bench")) cmd_bench();
    else if (sh_eq(cmd, "net")) cmd_net();
    else sh_emit("unknown"), g_cmd_ok = 0;
}

static void shell_run(Task *task, uint64_t ticks) {
    InputEvent ev;
    int key, y, i;
    static int s_had_focus;
    int focused;
    (void)ticks;
    if (ui_window(task, 0x00000000u, "Shell")) {
        s_had_focus = 0;
        return;
    }
    focused = task_is_focused(task) ? 1 : 0;
    if (focused && !s_had_focus) {
        input_clear_events();
    }
    s_had_focus = focused;
    if (focused) {
        while (input_next_event(&ev)) {
            if (ev.type == INPUT_EVENT_KEY && ev.key == INPUT_KEY_ENTER) {
                sh_emit(g_in);
                if (g_inlen) {
                    int h;
                    for (h = SH_HIST - 1; h > 0; h--)
                        sh_copy(g_hist[h], SH_LINE, g_hist[h - 1]);
                    sh_copy(g_hist[0], SH_LINE, g_in);
                    if (g_nhist < SH_HIST) g_nhist++;
                    g_hcur = -1;
                    sh_exec(g_in);
                }
                g_inlen = 0;
                g_in[0] = 0;
                continue;
            }
            if (ev.type == INPUT_EVENT_KEY && ev.key == INPUT_KEY_UP) {
                if (g_hcur + 1 < g_nhist) {
                    g_hcur++;
                    sh_copy(g_in, SH_LINE, g_hist[g_hcur]);
                    g_inlen = 0;
                    while (g_in[g_inlen]) g_inlen++;
                }
                continue;
            }
            key = 0;
            if (ev.type == INPUT_EVENT_TEXT &&
                (unsigned char)ev.character >= 32 &&
                (unsigned char)ev.character < 127)
                key = (int)(unsigned char)ev.character;
            if (ev.type == INPUT_EVENT_KEY && ev.key == INPUT_KEY_BACKSPACE)
                key = 8;
            if (key == 8 && g_inlen) {
                g_in[--g_inlen] = 0;
            } else if (key >= 32 && g_inlen < SH_LINE - 1) {
                g_in[g_inlen++] = (char)key;
                g_in[g_inlen] = 0;
            }
        }
    }
    y = task->frame.y + TASK_TITLE_HEIGHT + 2;
    for (i = 0; i < g_nlines; i++)
        ui_label(task->frame.x + 4, y + i * 16, task->frame.width - 8, 16,
                 g_lines[i], 0x0000FF00u);
    {
        char prompt[SH_LINE];
        int n = 0;
        prompt[n++] = '>';
        prompt[n++] = ' ';
        i = 0;
        while (g_in[i] && n < SH_LINE - 1) prompt[n++] = g_in[i++];
        prompt[n] = 0;
        ui_label(task->frame.x + 4, y + g_nlines * 16,
                 task->frame.width - 8, 16, prompt, 0x0000FF00u);
    }
}

void shell_window_open(void) {
    Task *ex = task_find(TASK_SHELL);
    TaskRect f;
    if (ex) {
        task_raise(ex->id);
        input_clear_events();
        return;
    }
    f.x = 40; f.y = UI_TASKBAR_HEIGHT + 20;
    f.width = 420; f.body_height = 300;
    (void)task_spawn(TASK_SHELL, f, shell_run);
    input_clear_events();
}
