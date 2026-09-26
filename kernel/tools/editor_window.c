/* LEARN:WS64-W07 */
#include "editor_window.h"

#include "cfs.h"
#include "editor.h"
#include "font.h"
#include "fs.h"
#include "graphics.h"
#include "input.h"
#include "lang_pipeline.h"
#include "task.h"
#include "ui.h"

#define EDITOR_X 80
#define EDITOR_Y 60
#define EDITOR_W 400
#define EDITOR_BODY_H 280
#define EDITOR_STATUS_H 18
#define EDITOR_CHROME_H 20
#define EDITOR_PAD_X 4
#define EDITOR_PAD_Y 2
#define EDITOR_CARET_W 2
#define EDITOR_FILEBUF (ED_MAX_LINES * ED_MAX_COLS + ED_MAX_LINES)

#define ED_MODE_EDIT 0
#define ED_MODE_NAME 1
#define ED_MODE_CONFIRM 2
#define ED_MODE_PICK 3
#define PICK_MAX 48


static Editor g_editor;
static int g_editor_inited;
static char g_filebuf[EDITOR_FILEBUF];
static char g_title[100];

static int ed_name_valid(const char *name) {
    unsigned int n = 0;
    unsigned int comp = 0;
    if (!name) {
        return 0;
    }
    while (name[n]) {
        if (name[n] == '\\') {
            return 0;
        }
        if (name[n] == '/') {
            if (comp < 1 || n >= ED_NAME - 1) {
                return 0;
            }
            comp = 0;
        } else {
            comp++;
            if (comp > CFS_NAME_MAX) {
                return 0;
            }
        }
        n++;
        if (n >= ED_NAME) {
            return 0;
        }
    }
    return n >= 1 && n < ED_NAME && comp >= 1;
}

static void ed_status_from_rc(Editor *e, int rc) {
    char buf[80];
    fs_err_status(buf, (int)sizeof(buf), rc, 0);
    ed_set_status(e, buf);
}

int ed_save(Editor *e) {
    int n;
    int rc;
    if (!e) {
        return CFS_EINVAL;
    }
    if (!ed_name_valid(e->name)) {
        if (!e->name[0]) {
            ed_set_status(e, "need name");
        } else {
            ed_set_status(e, "bad name");
        }
        return CFS_EINVAL;
    }
    ed_get_text(e, g_filebuf, EDITOR_FILEBUF);
    n = 0;
    while (g_filebuf[n]) {
        n++;
    }
    rc = fs_write(e->name, g_filebuf, n);
    if (rc < 0) {
        ed_status_from_rc(e, rc);
        return rc;
    }
    e->dirty = 0;
    ed_set_status(e, "saved");
    return CFS_OK;
}

int ed_open(Editor *e) {
    int rc;
    int i;
    if (!e) {
        return CFS_EINVAL;
    }
    if (!ed_name_valid(e->name)) {
        if (!e->name[0]) {
            ed_set_status(e, "need name");
        } else {
            ed_set_status(e, "bad name");
        }
        return CFS_EINVAL;
    }
    for (i = 0; i < EDITOR_FILEBUF; i++) {
        g_filebuf[i] = 0;
    }
    rc = fs_read(e->name, g_filebuf, EDITOR_FILEBUF - 1);
    if (rc < 0) {
        ed_status_from_rc(e, rc);
        return rc;
    }
    g_filebuf[rc] = 0;
    if (!ed_load_text(e, g_filebuf)) {
        return CFS_EFBIG;
    }
    e->dirty = 0;
    ed_set_status(e, "opened");
    return CFS_OK;
}

static void build_title(const Editor *e) {
    int i = 0;
    int s = 0;
    const char *src = e->name[0] ? e->name : "Editor";
    if (e->dirty) {
        g_title[i++] = '*';
    }
    while (src[s] && i < 98) {
        g_title[i++] = src[s++];
    }
    g_title[i] = 0;
}

static int map_input_event(const InputEvent *event) {
    unsigned char ch;
    if (!event) {
        return 0;
    }
    if (event->type == INPUT_EVENT_TEXT) {
        ch = (unsigned char)event->character;
        if (ch >= 32 && ch < 127) {
            return (int)ch;
        }
        return 0;
    }
    if (event->type != INPUT_EVENT_KEY) {
        return 0;
    }
    if (event->key == INPUT_KEY_BACKSPACE) {
        return 8;
    }
    if (event->key == INPUT_KEY_TAB) {
        return 9;
    }
    if (event->key == INPUT_KEY_ENTER) {
        return '\n';
    }
    if (event->key == INPUT_KEY_LEFT) {
        return ED_LEFT;
    }
    if (event->key == INPUT_KEY_RIGHT) {
        return ED_RIGHT;
    }
    if (event->key == INPUT_KEY_UP) {
        return ED_UP;
    }
    if (event->key == INPUT_KEY_DOWN) {
        return ED_DOWN;
    }
    if (event->key == INPUT_KEY_HOME) {
        return ED_HOME;
    }
    if (event->key == INPUT_KEY_END) {
        return ED_END;
    }
    if (event->key == INPUT_KEY_DELETE) {
        return ED_DEL;
    }
    return 0;
}

static void editor_auto_scroll(Editor *e, int visible_rows, int visible_cols) {
    ed_auto_scroll(e, visible_rows, visible_cols);
}

static void editor_draw_status(const Task *task, const Editor *e) {
    int x = task->frame.x;
    int y = task->frame.y + TASK_TITLE_HEIGHT + task->frame.body_height
            - EDITOR_STATUS_H;
    int w = task->frame.width;
    gfx_fill_rect(x, y, w, EDITOR_STATUS_H, 0x00D0D0D0u);
    gfx_draw_text_clipped(font_row, font_arial_width, font_arial_height,
                          e->status[0] ? e->status : "ready",
                          x + EDITOR_PAD_X, y + 1, CHRIS_TEXT_COLOR,
                          x, y, w, EDITOR_STATUS_H);
    if (lang_debug_paused()) {
        char dbg[40];
        unsigned pc = lang_debug_pc();
        int n = 0;
        dbg[n++] = 'p';
        dbg[n++] = 'c';
        dbg[n++] = '=';
        if (pc == 0) {
            dbg[n++] = '0';
        } else {
            char hex[8];
            int h = 0;
            unsigned v = pc;
            while (v && h < 8) {
                hex[h++] = "0123456789abcdef"[v & 15];
                v >>= 4;
            }
            while (h--)
                dbg[n++] = hex[h];
        }
        dbg[n] = 0;
        gfx_draw_text_clipped(font_row, font_arial_width, font_arial_height, dbg,
                              x + w / 2, y + 1, CHRIS_TEXT_COLOR,
                              x, y, w, EDITOR_STATUS_H);
    }
}

static void editor_draw_debug(const Task *task) {
    int x;
    int y;
    int w;
    int h = 64;
    char line[96];
    if (!task || !lang_debug_on())
        return;
    x = task->frame.x;
    w = task->frame.width;
    y = task->frame.y + TASK_TITLE_HEIGHT + task->frame.body_height
        - EDITOR_STATUS_H - h;
    gfx_fill_rect(x, y, w, h, 0x00182028u);
    (void)lang_debug_text(0, line, (int)sizeof(line));
    gfx_draw_text_clipped(font_row, font_arial_width, font_arial_height, line,
                          x + 4, y + 2, 0x00FFE080u, x, y, w, h);
    (void)lang_debug_text(1, line, (int)sizeof(line));
    gfx_draw_text_clipped(font_row, font_arial_width, font_arial_height, line,
                          x + 4, y + 16, 0x00C0E0C0u, x, y, w, h);
    (void)lang_debug_text(2, line, (int)sizeof(line));
    gfx_draw_text_clipped(font_row, font_arial_width, font_arial_height, line,
                          x + 4, y + 30, 0x00E0E0E0u, x, y, w, h);
    (void)lang_debug_text(5, line, (int)sizeof(line));
    gfx_draw_text_clipped(font_row, font_arial_width, font_arial_height, line,
                          x + 4, y + 44, 0x00A0C8E0u, x, y, w, h);
}

static void editor_draw_text(Task *task, Editor *e, uint64_t ticks) {
    int x = task->frame.x;
    int body_y = task->frame.y + TASK_TITLE_HEIGHT;
    int text_x = x + EDITOR_PAD_X;
    int text_y = body_y + EDITOR_CHROME_H + EDITOR_PAD_Y;
    int text_w = task->frame.width - EDITOR_PAD_X * 2;
    int dbg_h = lang_debug_on() ? 64 : 0;
    int text_h = task->frame.body_height - EDITOR_STATUS_H - EDITOR_CHROME_H
                 - EDITOR_PAD_Y * 2 - dbg_h;
    uint16_t dline = 0;
    int glyph_h = font_arial_height;
    int advance = gfx_text_advance(font_arial_width);
    int visible_rows;
    int visible_cols;
    int r;
    int caret_x;
    int caret_y;

    if (text_h < glyph_h) {
        text_h = glyph_h;
    }
    if (advance < 1) {
        advance = 1;
    }
    visible_rows = text_h / glyph_h;
    visible_cols = text_w / advance;
    if (lang_debug_paused())
        dline = lang_debug_line();
    editor_auto_scroll(e, visible_rows, visible_cols);
    task->state.editor.scroll_row = e->scroll_row;
    task->state.editor.scroll_col = e->scroll_col;

    for (r = 0; r < visible_rows; r++) {
        int line = e->scroll_row + r;
        int skip;
        int i;
        const char *src;
        if (line < 0 || line >= e->nlines) {
            continue;
        }
        src = e->lines[line];
        skip = e->scroll_col;
        i = 0;
        while (src[i] && i < skip) {
            i++;
        }
        if (dline && (int)dline == line + 1) {
            gfx_fill_rect(text_x, text_y + r * glyph_h, text_w, glyph_h,
                          0x00304058u);
        }
        if (lang_bp_has(line + 1)) {
            gfx_fill_rect(x + 1, text_y + r * glyph_h, 3, glyph_h, 0x00C04040u);
        }
        gfx_draw_text_clipped(font_row, font_arial_width, font_arial_height,
                              src + i,
                              text_x, text_y + r * glyph_h,
                              (dline && (int)dline == line + 1) ? 0x00FFE080u
                                                                : CHRIS_TEXT_COLOR,
                              text_x, text_y, text_w, text_h);
    }

    caret_x = text_x + (e->col - e->scroll_col) * advance;
    caret_y = text_y + (e->row - e->scroll_row) * glyph_h;
    if ((ticks / 30ull) % 2ull == 0ull &&
        e->row >= e->scroll_row &&
        e->row < e->scroll_row + visible_rows &&
        e->col >= e->scroll_col &&
        e->col <= e->scroll_col + visible_cols &&
        caret_x >= text_x &&
        caret_x + EDITOR_CARET_W <= text_x + text_w &&
        caret_y >= text_y &&
        caret_y + glyph_h <= text_y + text_h) {
        gfx_fill_rect(caret_x, caret_y, EDITOR_CARET_W, glyph_h,
                      CHRIS_TEXT_COLOR);
    }
}


static int g_mode;
static char g_namebuf[ED_NAME];
static int g_pending; /* 1=new after confirm, 2=open after confirm */
static char g_picks[PICK_MAX][ED_NAME];
static uint16_t g_pick_type[PICK_MAX];
static int g_npick;
static int g_psel;
static char g_pick_dir[FS_PATH];

static void name_from_editor(void) {
    int i = 0;
    while (g_editor.name[i] && i < ED_NAME - 1) {
        g_namebuf[i] = g_editor.name[i];
        i++;
    }
    g_namebuf[i] = 0;
}

static int pick_join(const char *dir, const char *name, char *out, int cap) {
    int i = 0;
    int j = 0;
    if (!out || cap < 2) {
        return 0;
    }
    if (dir && dir[0]) {
        while (dir[i] && i < cap - 1) {
            out[i] = dir[i];
            i++;
        }
        if (i < cap - 1 && name && name[0]) {
            out[i++] = '/';
        }
    }
    while (name && name[j] && i < cap - 1) {
        out[i++] = name[j++];
    }
    out[i] = 0;
    return 1;
}

static void pick_parent(void) {
    int n = 0;
    int slash = -1;
    while (g_pick_dir[n]) {
        if (g_pick_dir[n] == '/') {
            slash = n;
        }
        n++;
    }
    if (slash < 0) {
        g_pick_dir[0] = 0;
        return;
    }
    g_pick_dir[slash] = 0;
}

static int pick_cb(void *ctx, const char *name, uint32_t size, uint16_t type) {
    int *n = ctx;
    int i = 0;
    (void)size;
    if (*n >= PICK_MAX) {
        return 0;
    }
    while (name[i] && i < ED_NAME - 1) {
        g_picks[*n][i] = name[i];
        i++;
    }
    g_picks[*n][i] = 0;
    g_pick_type[*n] = type;
    (*n)++;
    return 0;
}

static void reload_picker(void) {
    g_npick = 0;
    g_psel = 0;
    (void)fs_list_at(g_pick_dir, pick_cb, &g_npick);
}

static void start_picker(void) {
    g_pick_dir[0] = 0;
    reload_picker();
    g_mode = ED_MODE_PICK;
}

static void pick_activate(Editor *e, int index) {
    char path[FS_PATH];
    if (index < 0 || index >= g_npick) {
        return;
    }
    if (!pick_join(g_pick_dir, g_picks[index], path, FS_PATH)) {
        ed_set_status(e, "path too long");
        return;
    }
    if (g_pick_type[index] == CFS_INODE_DIR) {
        pick_join(g_pick_dir, g_picks[index], g_pick_dir, FS_PATH);
        reload_picker();
        ed_set_status(e, g_pick_dir[0] ? g_pick_dir : "/");
        return;
    }
    ed_set_name(e, path);
    (void)ed_open(e);
    g_mode = ED_MODE_EDIT;
}

static void do_new(void) {
    ed_init(&g_editor);
    ed_set_name(&g_editor, "SRC/DEMO.CC");
    name_from_editor();
    g_mode = ED_MODE_NAME;
    ed_set_status(&g_editor, "type name, Enter");
}

static void request_new(void) {
    if (ed_is_dirty(&g_editor)) {
        g_pending = 1;
        g_mode = ED_MODE_CONFIRM;
        ed_set_status(&g_editor, "discard changes?");
        return;
    }
    do_new();
}

static void apply_name(void) {
    if (!ed_name_valid(g_namebuf)) {
        ed_set_status(&g_editor, "bad name");
        return;
    }
    ed_set_name(&g_editor, g_namebuf);
    g_mode = ED_MODE_EDIT;
    ed_set_status(&g_editor, "name set");
}

static void editor_run(Task *task, uint64_t ticks) {
    Editor *e;
    InputEvent event;
    int key;
    int bx;
    int by;
    int i;

    if (!task) {
        return;
    }
    e = &g_editor;
    build_title(e);
    if (ui_window(task, CHRIS_WINDOW_COLOR, g_title)) {
        return;
    }

    bx = task->frame.x + 4;
    by = task->frame.y + TASK_TITLE_HEIGHT + 2;
    if (g_mode == ED_MODE_CONFIRM) {
        ui_label(bx, by, 200, 16, "Discard unsaved?", CHRIS_TEXT_COLOR);
        if (ui_button(task, bx, by + 20, 40, 16, CHRIS_TASKBAR_COLOR, "Yes")) {
            if (g_pending == 1) {
                do_new();
            } else {
                start_picker();
            }
            g_pending = 0;
        }
        if (ui_button(task, bx + 48, by + 20, 40, 16, CHRIS_EDITOR_COLOR, "No")) {
            g_mode = ED_MODE_EDIT;
            g_pending = 0;
        }
        editor_draw_status(task, e);
        return;
    }
    if (g_mode == ED_MODE_NAME) {
        ui_label(bx, by, 80, 16, "Name:", CHRIS_TEXT_COLOR);
        gfx_fill_rect(bx + 50, by, task->frame.width - 60, 16, 0x00E0E0E0u);
        ui_label(bx + 52, by, task->frame.width - 64, 16, g_namebuf,
                 CHRIS_TEXT_COLOR);
        if (ui_button(task, bx, by + 20, 50, 16, CHRIS_TASKBAR_COLOR, "OK")) {
            apply_name();
        }
        if (ui_button(task, bx + 54, by + 20, 50, 16, CHRIS_EDITOR_COLOR, "Esc")) {
            g_mode = ED_MODE_EDIT;
        }
        if (task_is_focused(task)) {
            while (input_next_event(&event)) {
                if (event.type == INPUT_EVENT_KEY &&
                    event.key == INPUT_KEY_ENTER) {
                    apply_name();
                    continue;
                }
                if (event.type == INPUT_EVENT_KEY &&
                    event.key == INPUT_KEY_ESCAPE) {
                    g_mode = ED_MODE_EDIT;
                    continue;
                }
                key = map_input_event(&event);
                if (key == 8) {
                    int n = 0;
                    while (g_namebuf[n]) n++;
                    if (n) g_namebuf[n - 1] = 0;
                } else if (key >= 32 && key < 127) {
                    int n = 0;
                    while (g_namebuf[n]) n++;
                    if (n < ED_NAME - 1) {
                        g_namebuf[n] = (char)key;
                        g_namebuf[n + 1] = 0;
                    }
                }
            }
        }
        editor_draw_status(task, e);
        return;
    }
    if (g_mode == ED_MODE_PICK) {
        char label[80];
        int row_h = font_arial_height > 0 ? font_arial_height : 16;
        int nlab = 0;
        const char *shown = g_pick_dir[0] ? g_pick_dir : "/";
        label[nlab++] = 'O';
        label[nlab++] = 'p';
        label[nlab++] = 'e';
        label[nlab++] = 'n';
        label[nlab++] = ' ';
        while (shown[0] && nlab < 78) {
            label[nlab++] = *shown++;
        }
        label[nlab] = 0;
        ui_label(bx, by, task->frame.width - 8, row_h, label, CHRIS_TEXT_COLOR);
        if (ui_button(task, bx, by + row_h + 2, 36, row_h, CHRIS_TASKBAR_COLOR,
                      "Up")) {
            pick_parent();
            reload_picker();
        }
        for (i = 0; i < g_npick; i++) {
            int ry = by + row_h + 4 + row_h + i * row_h;
            char line[ED_NAME + 8];
            int k = 0;
            int s = 0;
            if (g_pick_type[i] == CFS_INODE_DIR) {
                line[k++] = '[';
                line[k++] = 'D';
                line[k++] = ']';
                line[k++] = ' ';
            }
            while (g_picks[i][s] && k < ED_NAME + 6) {
                line[k++] = g_picks[i][s++];
            }
            line[k] = 0;
            if (i == g_psel) {
                gfx_fill_rect(bx, ry, task->frame.width - 16, row_h, 0x00D8D0C4u);
            }
            ui_label(bx + 2, ry, task->frame.width - 20, row_h, line,
                     CHRIS_TEXT_COLOR);
            if (ui_row_click(task, bx, ry, task->frame.width - 16, row_h)) {
                g_psel = i;
                pick_activate(e, i);
            }
        }
        if (ui_button(task, bx, by + row_h + 4 + row_h + g_npick * row_h, 50,
                      row_h, CHRIS_EDITOR_COLOR, "Esc")) {
            g_mode = ED_MODE_EDIT;
        }
        editor_draw_status(task, e);
        return;
    }

    if (ui_button(task, bx, by, 36, 16, CHRIS_TASKBAR_COLOR, "New")) {
        request_new();
    }
    if (ui_button(task, bx + 40, by, 44, 16, CHRIS_TASKBAR_COLOR, "Name")) {
        name_from_editor();
        g_mode = ED_MODE_NAME;
    }
    if (ui_button(task, bx + 88, by, 44, 16, CHRIS_TASKBAR_COLOR, "Open")) {
        if (ed_is_dirty(e)) {
            g_pending = 2;
            g_mode = ED_MODE_CONFIRM;
        } else {
            start_picker();
        }
    }
    if (ui_button(task, bx + 136, by, 44, 16, CHRIS_TASKBAR_COLOR, "Save")) {
        (void)ed_save(e);
    }
    if (ui_button(task, bx + 184, by, 54, 16, CHRIS_TASKBAR_COLOR, "Compile")) {
        (void)lang_compile(e);
    }
    if (ui_button(task, bx + 242, by, 36, 16, CHRIS_TASKBAR_COLOR, "Go")) {
        (void)lang_compile_run(e);
    }

    if (task_is_focused(task)) {
        while (input_next_event(&event)) {
            if (event.type == INPUT_EVENT_KEY &&
                event.key == INPUT_KEY_F2) {
                (void)ed_save(e);
                continue;
            }
            if (event.type == INPUT_EVENT_KEY &&
                event.key == INPUT_KEY_F3) {
                start_picker();
                continue;
            }
            if (event.type == INPUT_EVENT_KEY &&
                event.key == INPUT_KEY_F4) {
                (void)lang_compile(e);
                continue;
            }
            if (event.type == INPUT_EVENT_KEY &&
                event.key == INPUT_KEY_F5) {
                if (input_key_down(0x2A) || input_key_down(0x36)) {
                    (void)lang_compile_run_jit(e);
                } else {
                    (void)lang_compile_run(e);
                }
                continue;
            }
            if (event.type == INPUT_EVENT_KEY &&
                event.key == INPUT_KEY_F7) {
                (void)lang_bp_toggle_line(e->row + 1);
                continue;
            }
            if (event.type == INPUT_EVENT_KEY &&
                event.key == INPUT_KEY_F9) {
                lang_debug_enable(1);
                (void)lang_compile_run(e);
                lang_debug_enable(0);
                continue;
            }
            if (event.type == INPUT_EVENT_KEY &&
                event.key == INPUT_KEY_F10) {
                if (lang_debug_paused()) {
                    if (input_key_down(0x2A) || input_key_down(0x36))
                        lang_debug_step_over();
                    else
                        lang_debug_step();
                } else {
                    lang_debug_continue();
                }
                continue;
            }
            if (event.type == INPUT_EVENT_KEY &&
                event.key == INPUT_KEY_F8) {
                if (input_key_down(0x2A) || input_key_down(0x36))
                    lang_debug_detach();
                else
                    lang_debug_continue();
                continue;
            }
            key = map_input_event(&event);
            if (key) {
                (void)ed_handle(e, key);
            }
        }
    }

    editor_draw_text(task, e, ticks);
    editor_draw_debug(task);
    editor_draw_status(task, e);
}

void editor_window_open(void) {
    Task *existing = task_find(TASK_EDITOR);
    TaskRect frame;
    Task *task;
    int id;

    if (existing) {
        task_raise(existing->id);
        return;
    }

    frame.x = EDITOR_X;
    frame.y = EDITOR_Y;
    frame.width = EDITOR_W;
    frame.body_height = EDITOR_BODY_H;
    id = task_spawn(TASK_EDITOR, frame, editor_run);
    if (id < 0) {
        return;
    }
    task = task_get(id);
    if (!task) {
        return;
    }
    if (!g_editor_inited) {
        ed_init(&g_editor);
        ed_set_name(&g_editor, "NOTES.TXT");
        g_editor_inited = 1;
    }
    task->state.editor.model_slot = 0;
    task->state.editor.scroll_row = g_editor.scroll_row;
    task->state.editor.scroll_col = g_editor.scroll_col;
}

void editor_window_open_path(const char *path) {
    editor_window_open();
    if (!path || !path[0]) {
        return;
    }
    ed_set_name(&g_editor, path);
    if (ed_open(&g_editor) != CFS_OK) {
        ed_init(&g_editor);
        ed_set_name(&g_editor, path);
        ed_set_status(&g_editor, "new file");
    }
}
