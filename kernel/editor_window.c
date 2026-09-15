/* LEARN:DESK64-08 */
#include "editor_window.h"

#include "editor.h"
#include "font.h"
#include "graphics.h"
#include "input.h"
#include "task.h"
#include "ui.h"

#define EDITOR_X 80
#define EDITOR_Y 60
#define EDITOR_W 400
#define EDITOR_BODY_H 280
#define EDITOR_STATUS_H 18
#define EDITOR_PAD_X 4
#define EDITOR_PAD_Y 2
#define EDITOR_CARET_W 2

static Editor g_editor;
static int g_editor_inited;

static int map_input_event(const InputEvent *event) {
    if (!event) {
        return 0;
    }
    if (event->type == INPUT_EVENT_TEXT) {
        unsigned char ch = (unsigned char)event->character;
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
    int text_y = body_y + EDITOR_PAD_Y;
    int text_w = task->frame.width - EDITOR_PAD_X * 2;
    int text_h = task->frame.body_height - EDITOR_STATUS_H - EDITOR_PAD_Y * 2;
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

    if (!task) {
        return;
    }
    e = &g_editor;
    if (ui_window(task, CHRIS_WINDOW_COLOR, "Editor")) {
        return;
    }

    if (task_is_focused(task)) {
        while (input_next_event(&event)) {
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
        g_editor_inited = 1;
    }
    task->state.editor.model_slot = 0;
    task->state.editor.scroll_row = g_editor.scroll_row;
    task->state.editor.scroll_col = g_editor.scroll_col;
}
