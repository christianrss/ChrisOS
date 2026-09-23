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

static void dbg_hex(char *dst, int cap, const char *lab, uint32_t v) {
    int n = 0;
    char hex[8];
    int h = 0;
    if (cap < 2) {
        return;
    }
    while (lab[n] && n + 1 < cap) {
        dst[n] = lab[n];
        n++;
    }
    if (v == 0) {
        if (n + 1 < cap)
            dst[n++] = '0';
        dst[n] = 0;
        return;
    }
    while (v && h < 8) {
        hex[h++] = "0123456789abcdef"[v & 15];
        v >>= 4;
    }
    while (h && n + 1 < cap)
        dst[n++] = hex[--h];
    dst[n] = 0;
}

static void editor_draw_debug(const Task *task) {
    int x;
    int y;
    int w = 108;
    int i;
    if (!task || !lang_debug_paused()) {
        return;
    }
    x = task->frame.x + task->frame.width - w - 2;
    y = task->frame.y + TASK_TITLE_HEIGHT + EDITOR_CHROME_H + 2;
    gfx_fill_rect(x, y, w, 86, 0x00202830u);
    {
        char line[24];
        dbg_hex(line, 24, "pc ", lang_debug_pc());
        gfx_draw_text_clipped(font_row, font_arial_width, font_arial_height, line,
                              x + 2, y + 2, 0x00E0E0E0u, x, y, w, 86);
        {
            unsigned ln = (unsigned)lang_debug_line();
            dbg_hex(line, 24, "ln ", ln);
            gfx_draw_text_clipped(font_row, font_arial_width, font_arial_height, line,
                                  x + 2, y + 16, 0x00E0E0E0u, x, y, w, 86);
        }
        for (i = 0; i < 2; ++i) {
            char lab[4];
            lab[0] = 's';
            lab[1] = (char)('0' + i);
            lab[2] = ' ';
            lab[3] = 0;
            dbg_hex(line, 24, lab, (uint32_t)lang_debug_stack(i));
            gfx_draw_text_clipped(font_row, font_arial_width, font_arial_height,
                                  line, x + 2, y + 30 + i * 14, 0x00E0E0E0u,
                                  x, y, w, 86);
        }
        dbg_hex(line, 24, "m0 ", (uint32_t)lang_debug_mem(0));
        gfx_draw_text_clipped(font_row, font_arial_width, font_arial_height, line,
                              x + 2, y + 58, 0x00E0E0E0u, x, y, w, 86);
        dbg_hex(line, 24, "m8 ", (uint32_t)lang_debug_mem(8));
        gfx_draw_text_clipped(font_row, font_arial_width, font_arial_height, line,
                              x + 2, y + 72, 0x00E0E0E0u, x, y, w, 86);
    }
}

static void editor_draw_text(Task *task, Editor *e, uint64_t ticks) {
    int x = task->frame.x;
    int body_y = task->frame.y + TASK_TITLE_HEIGHT;
    int text_x = x + EDITOR_PAD_X;
    int text_y = body_y + EDITOR_CHROME_H + EDITOR_PAD_Y;
    int text_w = task->frame.width - EDITOR_PAD_X * 2;
    int text_h = task->frame.body_height - EDITOR_STATUS_H - EDITOR_CHROME_H
                 - EDITOR_PAD_Y * 2;
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
        gfx_draw_text_clipped(font_row, font_arial_width, font_arial_height,
                              src + i,
                              text_x, text_y + r * glyph_h, CHRIS_TEXT_COLOR,
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
static int g_npick;
static int g_psel;

static void name_from_editor(void) {
    int i = 0;
    while (g_editor.name[i] && i < ED_NAME - 1) {
        g_namebuf[i] = g_editor.name[i];
        i++;
    }
    g_namebuf[i] = 0;
}

static int pick_cb(void *ctx, const char *name, uint32_t size, uint16_t type) {
    int *n = ctx;
    int i = 0;
    (void)size;
    if (*n >= PICK_MAX) {
        return 0;
    }
    if (type == CFS_INODE_DIR) {
        g_picks[*n][0] = '/';
        while (name[i] && i < ED_NAME - 2) {
            g_picks[*n][i + 1] = name[i];
            i++;
        }
        g_picks[*n][i + 1] = 0;
    } else {
        while (name[i] && i < ED_NAME - 1) {
            g_picks[*n][i] = name[i];
            i++;
        }
        g_picks[*n][i] = 0;
    }
    (*n)++;
    return 0;
}

static void start_picker(void) {
    g_npick = 0;
    g_psel = 0;
    (void)fs_list_at("", pick_cb, &g_npick);
    g_mode = ED_MODE_PICK;
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
        ui_label(bx, by, 200, 16, "Open file (root)", CHRIS_TEXT_COLOR);
        for (i = 0; i < g_npick; i++) {
            int ry = by + 20 + i * 16;
            if (i == g_psel) {
                gfx_fill_rect(bx, ry, task->frame.width - 16, 16, 0x00C0C0C0u);
            }
            ui_label(bx + 2, ry, task->frame.width - 20, 16, g_picks[i],
                     CHRIS_TEXT_COLOR);
            if (task_is_focused(task) &&
                ui_hit_rect(input_mouse_snapshot().x, input_mouse_snapshot().y,
                            bx, ry, task->frame.width - 16, 16) &&
                input_left_pressed()) {
                g_psel = i;
                input_consume_left_press();
                if (g_picks[i][0] == '/') {
                    ed_set_status(e, "pick a file");
                } else {
                    ed_set_name(e, g_picks[i]);
                    (void)ed_open(e);
                    g_mode = ED_MODE_EDIT;
                }
            }
        }
        if (ui_button(task, bx, by + 20 + g_npick * 16, 50, 16,
                      CHRIS_EDITOR_COLOR, "Esc")) {
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
                continue;
            }
            if (event.type == INPUT_EVENT_KEY &&
                event.key == INPUT_KEY_F10) {
                if (lang_debug_paused())
                    lang_debug_step();
                else
                    lang_debug_continue();
                continue;
            }
            if (event.type == INPUT_EVENT_KEY &&
                event.key == INPUT_KEY_F8) {
                lang_debug_enable(0);
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
