/* LEARN:WS64-W06 */
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
    if (rc == CFS_ENOSPC) {
        ed_set_status(e, "disk full");
        return;
    }
    if (rc == CFS_ENOENT) {
        ed_set_status(e, "not found");
        return;
    }
    if (rc == CFS_EFBIG) {
        ed_set_status(e, "file too big");
        return;
    }
    if (rc == CFS_ENAMETOOLONG) {
        ed_set_status(e, "name too long");
        return;
    }
    if (rc == CFS_EINVAL) {
        ed_set_status(e, "bad name");
        return;
    }
    if (rc == CFS_EIO) {
        ed_set_status(e, "io error");
        return;
    }
    if (rc == CFS_ENOTMOUNTED) {
        ed_set_status(e, "not mounted");
        return;
    }
    ed_set_status(e, "io error");
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
    if (visible_rows < 1) {
        visible_rows = 1;
    }
    if (visible_cols < 1) {
        visible_cols = 1;
    }
    if (e->row < e->scroll_row) {
        e->scroll_row = e->row;
    }
    if (e->row >= e->scroll_row + visible_rows) {
        e->scroll_row = e->row - visible_rows + 1;
    }
    if (e->scroll_row < 0) {
        e->scroll_row = 0;
    }
    if (e->col < e->scroll_col) {
        e->scroll_col = e->col;
    }
    if (e->col >= e->scroll_col + visible_cols) {
        e->scroll_col = e->col - visible_cols + 1;
    }
    if (e->scroll_col < 0) {
        e->scroll_col = 0;
    }
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

static void editor_run(Task *task, uint64_t ticks) {
    Editor *e;
    InputEvent event;
    int key;
    int bx;
    int by;

    if (!task) {
        return;
    }
    e = &g_editor;
    build_title(e);
    if (ui_window(task, CHRIS_WINDOW_COLOR, g_title)) {
        return;
    }

    bx = task->frame.x + 8;
    by = task->frame.y + TASK_TITLE_HEIGHT + 2;
    if (ui_button(task, bx, by, 44, 16, CHRIS_TASKBAR_COLOR, "Save")) {
        (void)lang_save(e);
    }
    if (ui_button(task, bx + 50, by, 44, 16, CHRIS_TASKBAR_COLOR, "Open")) {
        (void)ed_open(e);
    }
    if (ui_button(task, bx + 100, by, 54, 16, CHRIS_TASKBAR_COLOR, "Compile")) {
        (void)lang_compile(e);
    }
    if (ui_button(task, bx + 158, by, 36, 16, CHRIS_TASKBAR_COLOR, "Go")) {
        (void)lang_compile_run(e);
    }

    if (task_is_focused(task)) {
        while (input_next_event(&event)) {
            if (event.type == INPUT_EVENT_KEY &&
                event.key == INPUT_KEY_F2) {
                (void)lang_save(e);
                continue;
            }
            if (event.type == INPUT_EVENT_KEY &&
                event.key == INPUT_KEY_F3) {
                (void)ed_open(e);
                continue;
            }
            if (event.type == INPUT_EVENT_KEY &&
                event.key == INPUT_KEY_F4) {
                (void)lang_compile(e);
                continue;
            }
            if (event.type == INPUT_EVENT_KEY &&
                event.key == INPUT_KEY_F5) {
                (void)lang_compile_run(e);
                continue;
            }
            key = map_input_event(&event);
            if (key) {
                (void)ed_handle(e, key);
            }
        }
    }

    editor_draw_text(task, e, ticks);
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
