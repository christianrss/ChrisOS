/* LEARN:WS64-W08 */
#include "app_window.h"

#include "clvm_sys.h"
#include "font.h"
#include "graphics.h"
#include "input.h"
#include "lang_pipeline.h"
#include "task.h"
#include "ui.h"

static int g_window_cascade = 1;

#define APP_CHROME_H 20
#define APP_BTN_W 22

static void app_title(char *dst, const char *name) {
    int i = 0;
    int slash = 0;
    if (!name || !name[0]) {
        name = "App";
    }
    while (name[i]) {
        if (name[i] == '/') {
            slash = i + 1;
        }
        i++;
    }
    name += slash;
    i = 0;
    while (name[i] && i < TASK_TITLE_CHARS - 1) {
        dst[i] = name[i];
        i++;
    }
    dst[i] = 0;
}

static int app_is_hd_game(int gw, int gh) {
    return gw >= 640 && gh >= 480 && gw <= 1024 && gh <= 768;
}

static int app_is_game(int gw, int gh) {
    if (app_is_hd_game(gw, gh)) {
        return 1;
    }
    return gw > 0 && gh > 0 && gw <= CLVM_SYS_GAME_W + 32 &&
           gh <= CLVM_SYS_GAME_H + 32;
}

static int app_game_chrome(Task *task, int slot) {
    InputMouse mouse;
    int x;
    int y;
    int w;
    int h;
    int close_bx;
    int max_bx;
    int min_bx;
    int close_w = APP_BTN_W;
    bool close_hover;
    bool max_hover;
    bool min_hover;
    bool title_hover;
    bool resize_hover;
    const char *title;

    if (!task || !task->active) {
        return 1;
    }

    mouse = input_mouse_snapshot();
    x = task->frame.x;
    y = task->frame.y;
    w = task->frame.width;
    h = task->frame.body_height;
    if (w < close_w * 3 + 8) {
        close_w = w / 3;
        if (close_w < 16) {
            close_w = 16;
        }
    }
    close_bx = x + w - close_w;
    max_bx = close_bx - close_w;
    min_bx = max_bx - close_w;
    close_hover = ui_hit_rect(mouse.x, mouse.y, close_bx, y, close_w,
                              APP_CHROME_H);
    max_hover = ui_hit_rect(mouse.x, mouse.y, max_bx, y, close_w, APP_CHROME_H);
    min_hover = ui_hit_rect(mouse.x, mouse.y, min_bx, y, close_w, APP_CHROME_H);
    title_hover = task_is_focused(task) &&
                  ui_hit_rect(mouse.x, mouse.y, x, y, w - close_w * 3,
                              APP_CHROME_H);
    resize_hover = task_is_focused(task) &&
                   task->window.mode == TASK_WINDOW_NORMAL &&
                   ui_hit_rect(mouse.x, mouse.y, x + w - 8, y + h - 8, 8, 8);

    if (!mouse.left_down) {
        task->window.dragging = false;
        task->window.resizing = false;
    }

    if (task->window.resizing && mouse.left_down) {
        int nw = task->window.resize_start.width + mouse.x -
                 task->window.resize_mouse_x;
        int nh = task->window.resize_start.body_height + mouse.y -
                 task->window.resize_mouse_y;
        if (nw < 160) {
            nw = 160;
        }
        if (nh < APP_CHROME_H + 80) {
            nh = APP_CHROME_H + 80;
        }
        if (x + nw > g_gfx.width) {
            nw = g_gfx.width - x;
        }
        if (y + nh > g_gfx.height - UI_TASKBAR_HEIGHT) {
            nh = g_gfx.height - UI_TASKBAR_HEIGHT - y;
        }
        if (nh < APP_CHROME_H + 40) {
            nh = APP_CHROME_H + 40;
        }
        task_resize(task->id, nw, nh);
        w = task->frame.width;
        h = task->frame.body_height;
    } else if (task->window.dragging && mouse.left_down) {
        int nx = mouse.x - task->window.drag_offset_x;
        int ny = mouse.y - task->window.drag_offset_y;
        if (ny < 0) {
            ny = 0;
        }
        if (nx < 0) {
            nx = 0;
        }
        if (nx + task->frame.width > g_gfx.width) {
            nx = g_gfx.width - task->frame.width;
        }
        if (ny + task->frame.body_height > g_gfx.height - UI_TASKBAR_HEIGHT) {
            ny = g_gfx.height - UI_TASKBAR_HEIGHT - task->frame.body_height;
            if (ny < 0) {
                ny = 0;
            }
        }
        task_move(task->id, nx, ny);
        x = task->frame.x;
        y = task->frame.y;
        w = task->frame.width;
        h = task->frame.body_height;
        close_bx = x + w - close_w;
        max_bx = close_bx - close_w;
        min_bx = max_bx - close_w;
    } else if (resize_hover && input_left_pressed()) {
        task->window.resizing = true;
        task->window.resize_mouse_x = mouse.x;
        task->window.resize_mouse_y = mouse.y;
        task->window.resize_start = task->frame;
        input_consume_left_press();
    } else if (title_hover && input_left_pressed()) {
        task->window.dragging = true;
        task->window.drag_offset_x = mouse.x - x;
        task->window.drag_offset_y = mouse.y - y;
        input_consume_left_press();
    }

    gfx_fill_rect(x, y, w, APP_CHROME_H, CHRIS_TITLE_COLOR);
    gfx_fill_rect(x, y, w, 1, CHRIS_BORDER_COLOR);
    title = task_title(task);
    gfx_draw_text_clipped(font_row, font_arial_width, font_arial_height, title,
                          x + 6, y + 3, CHRIS_TITLE_TEXT, x, y, w - close_w * 3,
                          APP_CHROME_H);
    gfx_fill_rect(min_bx, y, close_w, APP_CHROME_H,
                  min_hover ? CHRIS_ACCENT_COLOR : CHRIS_EDITOR_COLOR);
    gfx_draw_text_clipped(font_row, font_arial_width, font_arial_height, "_",
                          min_bx + 6, y + 2, CHRIS_TITLE_TEXT, min_bx, y, close_w,
                          APP_CHROME_H);
    gfx_fill_rect(max_bx, y, close_w, APP_CHROME_H,
                  max_hover ? CHRIS_ACCENT_COLOR : CHRIS_EDITOR_COLOR);
    gfx_draw_text_clipped(font_row, font_arial_width, font_arial_height,
                          task->window.mode == TASK_WINDOW_MAXIMIZED ? "R" : "M",
                          max_bx + 6, y + 2, CHRIS_TITLE_TEXT, max_bx, y, close_w,
                          APP_CHROME_H);
    gfx_fill_rect(close_bx, y, close_w, APP_CHROME_H,
                  close_hover ? 0x00E07050u : 0x00804030u);
    gfx_draw_text_clipped(font_row, font_arial_width, font_arial_height, "X",
                          close_bx + 6, y + 2, CHRIS_TITLE_TEXT, close_bx, y, close_w,
                          APP_CHROME_H);
    gfx_fill_rect(x + w - 8, y + h - 8, 8, 8, 0x007090B0u);

    if (close_hover && input_left_pressed()) {
        input_consume_left_press();
        task_close(task->id);
        lang_slot_request_close(slot);
        return 1;
    }
    if (max_hover && input_left_pressed()) {
        if (task->window.mode == TASK_WINDOW_MAXIMIZED) {
            task_restore(task->id);
        } else {
            TaskRect bounds;
            bounds.x = 0;
            bounds.y = 0;
            bounds.width = g_gfx.width;
            bounds.body_height = g_gfx.height - UI_TASKBAR_HEIGHT;
            if (bounds.body_height < APP_CHROME_H + 80) {
                bounds.body_height = APP_CHROME_H + 80;
            }
            task_maximize(task->id, bounds);
        }
        input_consume_left_press();
    } else if (min_hover && input_left_pressed()) {
        input_consume_left_press();
        task_minimize(task->id);
        return 1;
    }
    return 0;
}

static void app_run(Task *task, uint64_t ticks) {
    int slot;
    uint32_t *pix;
    int gw;
    int gh;
    int game;
    int cx;
    int cy;
    int cw;
    int ch;
    (void)ticks;
    if (!task) {
        return;
    }
    slot = task->state.app.lang_slot;
    if (!lang_slot_used(slot)) {
        task_close(task->id);
        return;
    }
    pix = lang_slot_pixels(slot);
    if (!pix) {
        return;
    }
    gw = lang_slot_w(slot);
    gh = lang_slot_h(slot);
    game = app_is_game(gw, gh);
    if (game) {
        if (app_game_chrome(task, slot)) {
            return;
        }
        if (task->window.mode == TASK_WINDOW_MINIMIZED) {
            return;
        }
        cx = task->frame.x;
        cy = task->frame.y + APP_CHROME_H;
        cw = task->frame.width;
        ch = task->frame.body_height - APP_CHROME_H;
        if (cw < 1) {
            cw = 1;
        }
        if (ch < 1) {
            ch = 1;
        }
        clvm_sys_blit_scaled(pix, gw, gh, cx, cy, cw, ch);
        return;
    }
    /* UI apps draw their own chrome; keep the slot 1:1 with the frame. */
    if (task->frame.width != gw) {
        task->frame.width = gw;
    }
    if (task->frame.body_height != gh) {
        task->frame.body_height = gh;
    }
    clvm_sys_blit_to(pix, task->frame.x, task->frame.y, gw, gh, gw, gh);
    if (task->frame.x == 0 && task->frame.y == 0 &&
        gw >= g_gfx.width && gh >= g_gfx.height) {
        ui_paint_desktop();
        return;
    }
    if (gh <= UI_TASKBAR_HEIGHT + 8 &&
        task->frame.y >= g_gfx.height - gh - 2 &&
        task->frame.width >= g_gfx.width - 4) {
        ui_paint_taskbar_strip(task->frame.y);
        return;
    }
    ui_paint_title(task->frame.x, task->frame.y, task->frame.width, 20,
                   task_title(task),
                   task->window.mode == TASK_WINDOW_MAXIMIZED);
    gfx_fill_rect(task->frame.x, task->frame.y + task->frame.body_height - 1,
                  task->frame.width, 1, CHRIS_BORDER_COLOR);
    gfx_fill_rect(task->frame.x, task->frame.y, 1, task->frame.body_height,
                  CHRIS_BORDER_COLOR);
    gfx_fill_rect(task->frame.x + task->frame.width - 1, task->frame.y, 1,
                  task->frame.body_height, CHRIS_BORDER_COLOR);
}

void app_window_open(int lang_slot, const char *title) {
    TaskRect frame;
    int id;
    Task *task;
    int i;
    char named[TASK_TITLE_CHARS];
    int gw;
    int gh;
    int offset;
    int game;

    app_title(named, title);
    for (i = 0; i < task_count(); i++) {
        Task *t = task_iter(i);
        if (t && t->type == TASK_APP && t->state.app.lang_slot == lang_slot) {
            task_set_title(t->id, named);
            task_raise(t->id);
            return;
        }
    }
    gw = lang_slot_w(lang_slot);
    gh = lang_slot_h(lang_slot);
    if (gw < 64) {
        gw = CLVM_SYS_GAME_W;
    }
    if (gh < 32) {
        gh = CLVM_SYS_GAME_H;
    }
    game = app_is_game(gw, gh);
    if (gw >= g_gfx.width && gh >= g_gfx.height) {
        frame.x = 0;
        frame.y = 0;
        frame.width = gw;
        frame.body_height = gh;
    } else {
        offset = g_window_cascade * 28;
        if (game) {
            int scale = app_is_hd_game(gw, gh) ? 1 : 2;
            frame.width = gw * scale;
            frame.body_height = gh * scale + APP_CHROME_H;
            if (frame.width > g_gfx.width - 48) {
                frame.width = g_gfx.width - 48;
            }
            if (frame.body_height > g_gfx.height - UI_TASKBAR_HEIGHT - 48) {
                frame.body_height = g_gfx.height - UI_TASKBAR_HEIGHT - 48;
            }
            if (frame.width < gw) {
                frame.width = gw;
            }
            if (frame.body_height < gh + APP_CHROME_H) {
                frame.body_height = gh + APP_CHROME_H;
            }
        } else {
            frame.width = gw;
            frame.body_height = gh;
        }
        frame.x = g_gfx.width - frame.width - 48 - (offset % 56);
        if (frame.x < 48) {
            frame.x = 48 + offset;
        }
        frame.y = 48 + offset;
        if (frame.y + frame.body_height > g_gfx.height - UI_TASKBAR_HEIGHT) {
            frame.y = 8;
        }
        ++g_window_cascade;
        if (g_window_cascade > 8) {
            g_window_cascade = 1;
        }
    }
    id = task_spawn(TASK_APP, frame, app_run);
    if (id < 0) {
        lang_kill(lang_slot);
        return;
    }
    task = task_get(id);
    if (!task) {
        return;
    }
    task->state.app.lang_slot = lang_slot;
    lang_bind_task(lang_slot, id);
    task_set_title(id, named);
    if (!(frame.x == 0 && frame.y == 0 && frame.width >= g_gfx.width)) {
        task_raise(id);
    }
}
