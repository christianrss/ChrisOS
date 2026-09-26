/* LEARN:WS64-W03 */
#include "ui.h"

#include "font.h"
#include "graphics.h"
#include "vgpu.h"
#include "icons.h"
#include "input.h"
#include "pit.h"

bool ui_hit_rect(int px, int py, int x, int y, int width, int height) {
    return width > 0 && height > 0 &&
           px >= x && px < x + width &&
           py >= y && py < y + height;
}

bool ui_row_click(Task *owner, int x, int y, int width, int height) {
    InputMouse mouse;
    if (owner && !task_is_focused(owner)) {
        return false;
    }
    mouse = input_mouse_snapshot();
    if (!ui_hit_rect(mouse.x, mouse.y, x, y, width, height)) {
        return false;
    }
    if (!input_left_pressed()) {
        return false;
    }
    input_consume_left_press();
    return true;
}

void ui_label(int x, int y, int w, int h, const char *text, uint32_t color) {
    if (!text) {
        return;
    }
    gfx_draw_text_clipped(font_row, font_arial_width, font_arial_height,
                          text, x, y, color, x, y, w, h);
}

bool ui_button(Task *owner, int x, int y, int width, int height,
               uint32_t color, const char *text) {
    InputMouse mouse = input_mouse_snapshot();
    bool allowed = (owner == 0) || task_is_focused(owner);
    bool hover = allowed && ui_hit_rect(mouse.x, mouse.y, x, y, width, height);
    int text_y = y + (height > font_arial_height ? (height - font_arial_height) / 2 : 0);
    uint32_t face = hover ? CHRIS_ACCENT_COLOR : color;
    uint32_t ink = hover ? CHRIS_TEXT_COLOR : CHRIS_TITLE_TEXT;

    if (width > 2 && height > 2) {
        gfx_fill_rect(x, y, width, height, CHRIS_BORDER_COLOR);
        gfx_fill_rect(x + 1, y + 1, width - 2, height - 2, face);
    } else {
        gfx_fill_rect(x, y, width, height, face);
    }
    if (text != 0) {
        gfx_draw_text_clipped(font_row, font_arial_width, font_arial_height,
                              text, x + 4, text_y, ink,
                              x, y, width, height);
    }
    if (hover && input_left_pressed()) {
        input_consume_left_press();
        return true;
    }
    return false;
}

static int icon_id_for(const char *caption) {
    if (!caption) {
        return ICON_LOGO;
    }
    if (caption[0] == 'S' && caption[1] == 'h') {
        return ICON_SHELL;
    }
    if (caption[0] == 'F') {
        return ICON_FILES;
    }
    if (caption[0] == 'E') {
        return ICON_EDITOR;
    }
    if (caption[0] == 'T') {
        return ICON_TASKS;
    }
    if (caption[0] == 'B') {
        return ICON_LOGO;
    }
    if (caption[0] == 'D') {
        return ICON_START;
    }
    if (caption[0] == 'P') {
        return ICON_START;
    }
    if (caption[0] == 'M' && (caption[1] == 'i' || caption[1] == 'I')) {
        return ICON_MINE;
    }
    return ICON_LOGO;
}

static void blit_icon_id(int id, int x, int y, int w, int h) {
    const RgbaImage *img = icon_by_id(id);
    if (!img || !img->px || img->w < 1 || img->h < 1) {
        gfx_fill_rect(x, y, w, h, CHRIS_ACCENT_COLOR);
        return;
    }
    gfx_blit_rgba(img->px, img->w, img->h, x, y, w, h);
}

bool ui_icon(int x, int y, uint32_t color, const char *caption) {
    InputMouse mouse = input_mouse_snapshot();
    int tw = UI_ICON_SIZE + 8;
    int th = UI_ICON_SIZE + UI_ICON_TEXT_H;
    bool hover = ui_hit_rect(mouse.x, mouse.y, x, y, tw, th);
    (void)color;

    blit_icon_id(icon_id_for(caption), x, y, UI_ICON_SIZE, UI_ICON_SIZE);
    if (hover) {
        gfx_fill_rect(x, y + UI_ICON_SIZE - 2, UI_ICON_SIZE, 2, CHRIS_ACCENT_COLOR);
    }
    if (caption) {
        ui_label(x, y + UI_ICON_SIZE + 1, tw, UI_ICON_TEXT_H,
                 caption, CHRIS_TITLE_TEXT);
    }
    if (hover && input_left_pressed() && mouse.y >= UI_TASKBAR_HEIGHT) {
        input_consume_left_press();
        return true;
    }
    return false;
}

void ui_paint_desktop(void) {
    static const char *caps[] = {
        "Shell", "Files", "Edit", "Tasks", "Ball", "Doom", "Prefs", "Mine Chris"
    };
    const RgbaImage *wall = icon_wallpaper();
    int dw = g_gfx.width;
    int dh = g_gfx.height;
    int gap = 16;
    int col_w = 96;
    int i;
    int col = 0;
    int row = 0;

    gfx_fill_rect(0, 0, dw, dh, CHRIS_DESKTOP_COLOR);
    if (wall && wall->px && wall->w > 0 && wall->h > 0 &&
        wall->w <= dw && wall->h <= dh) {
        gfx_blit_rgba(wall->px, wall->w, wall->h,
                      (dw - wall->w) / 2, (dh - wall->h) / 2,
                      wall->w, wall->h);
    }
    for (i = 0; i < 8; ++i) {
        int ix = gap + col * col_w;
        int iy = gap + row * 72;
        if (iy + 72 > dh - UI_TASKBAR_HEIGHT - 8) {
            col++;
            row = 0;
            ix = gap + col * col_w;
            iy = gap;
        }
        blit_icon_id(icon_id_for(caps[i]), ix, iy, 48, 48);
        ui_label(ix, iy + 52, col_w - 4, 16, caps[i], CHRIS_TITLE_TEXT);
        row++;
    }
}

void ui_paint_taskbar_strip(int y) {
    static const char *names[] = {
        "Shell", "Files", "Edit", "Tasks", "Ball", "Doom", "Prefs"
    };
    char clock[6];
    unsigned sec;
    unsigned m;
    unsigned s;
    int i;
    int h = UI_TASKBAR_HEIGHT;
    if (y < 0) {
        y = 0;
    }
    gfx_fill_rect(0, y, g_gfx.width, h, CHRIS_TASKBAR_COLOR);
    gfx_fill_rect(0, y, g_gfx.width, 1, CHRIS_ACCENT_COLOR);
    ui_label(8, y + 12, 70, 16, "ChrisOS", CHRIS_TITLE_TEXT);
    for (i = 0; i < 7; ++i) {
        int x = 80 + i * 72;
        gfx_fill_rect(x, y + 8, 64, 24, CHRIS_EDITOR_COLOR);
        gfx_fill_rect(x, y + 8, 64, 1, CHRIS_BORDER_COLOR);
        ui_label(x + 8, y + 12, 52, 16, names[i], CHRIS_TITLE_TEXT);
    }
    sec = (unsigned)(pit_ticks() / 60ull);
    m = (sec / 60u) % 60u;
    s = sec % 60u;
    clock[0] = (char)('0' + (m / 10u));
    clock[1] = (char)('0' + (m % 10u));
    clock[2] = ':';
    clock[3] = (char)('0' + (s / 10u));
    clock[4] = (char)('0' + (s % 10u));
    clock[5] = 0;
    ui_label(g_gfx.width - 56, y + 12, 48, 16, clock, CHRIS_TITLE_TEXT);
}

void ui_paint_title(int x, int y, int w, int h, const char *title, int maximized) {
    int bw = 22;
    if (w < 80 || h < 16) {
        return;
    }
    gfx_fill_rect(x, y, w, h, CHRIS_TITLE_COLOR);
    gfx_fill_rect(x, y, w, 1, CHRIS_BORDER_COLOR);
    gfx_fill_rect(x, y + h - 1, w, 1, CHRIS_BORDER_COLOR);
    gfx_fill_rect(x, y, 1, h, CHRIS_BORDER_COLOR);
    gfx_fill_rect(x + w - 1, y, 1, h, CHRIS_BORDER_COLOR);
    if (title) {
        gfx_draw_text_clipped(font_row, font_arial_width, font_arial_height,
                              title, x + 6, y + 2, CHRIS_TITLE_TEXT,
                              x, y, w - bw * 3, h);
    }
    gfx_fill_rect(x + w - bw * 3, y, bw, h, CHRIS_EDITOR_COLOR);
    gfx_fill_rect(x + w - bw * 2, y, bw, h, CHRIS_EDITOR_COLOR);
    gfx_fill_rect(x + w - bw, y, bw, h, 0x00804030u);
    gfx_draw_text_clipped(font_row, font_arial_width, font_arial_height,
                          "_", x + w - bw * 3 + 6, y + 2, CHRIS_TITLE_TEXT,
                          x + w - bw * 3, y, bw, h);
    gfx_draw_text_clipped(font_row, font_arial_width, font_arial_height,
                          maximized ? "R" : "M", x + w - bw * 2 + 6, y + 2, CHRIS_TITLE_TEXT,
                          x + w - bw * 2, y, bw, h);
    gfx_draw_text_clipped(font_row, font_arial_width, font_arial_height,
                          "X", x + w - bw + 6, y + 2, CHRIS_TITLE_TEXT,
                          x + w - bw, y, bw, h);
}

bool ui_window_ex(Task *task, uint32_t body_color, const char *title,
                  int fill_body) {
    InputMouse mouse;
    int x;
    int y;
    int close_bx;
    int max_bx;
    int min_bx;
    int close_w = 22;
    bool close_hover;
    bool max_hover;
    bool min_hover;
    bool title_hover;
    bool resize_hover;

    if (task == 0 || !task->active) {
        return false;
    }

    mouse = input_mouse_snapshot();
    x = task->frame.x;
    y = task->frame.y;
    if (task->frame.width < close_w + 8) {
        close_w = task->frame.width / 3;
        if (close_w < 16) {
            close_w = 16;
        }
    }
    close_bx = x + task->frame.width - close_w;
    max_bx = close_bx - close_w;
    min_bx = max_bx - close_w;
    close_hover = ui_hit_rect(mouse.x, mouse.y, close_bx, y,
                              close_w, TASK_TITLE_HEIGHT);
    max_hover = ui_hit_rect(mouse.x, mouse.y, max_bx, y,
                            close_w, TASK_TITLE_HEIGHT);
    min_hover = ui_hit_rect(mouse.x, mouse.y, min_bx, y,
                            close_w, TASK_TITLE_HEIGHT);
    title_hover = task_is_focused(task) &&
                  ui_hit_rect(mouse.x, mouse.y, x, y,
                              task->frame.width - close_w * 3,
                              TASK_TITLE_HEIGHT);
    resize_hover = task_is_focused(task) &&
                   task->window.mode == TASK_WINDOW_NORMAL &&
                   ui_hit_rect(mouse.x, mouse.y,
                               x + task->frame.width - 6,
                               y + task->frame.body_height - 6, 6, 6);

    if (!mouse.left_down) {
        task->window.dragging = false;
        task->window.resizing = false;
    }

    if (task->window.resizing && mouse.left_down) {
        int nw = task->window.resize_start.width +
                 mouse.x - task->window.resize_mouse_x;
        int nh = task->window.resize_start.body_height +
                 mouse.y - task->window.resize_mouse_y;
        if (nw < 160)
            nw = 160;
        if (nh < 100)
            nh = 100;
        if (x + nw > g_gfx.width)
            nw = g_gfx.width - x;
        if (y + nh > g_gfx.height)
            nh = g_gfx.height - y;
        task_resize(task->id, nw, nh);
    } else if (task->window.dragging && mouse.left_down) {
        int nx = mouse.x - task->window.drag_offset_x;
        int ny = mouse.y - task->window.drag_offset_y;
        if (ny < UI_TASKBAR_HEIGHT) {
            ny = UI_TASKBAR_HEIGHT;
        }
        if (nx < 0)
            nx = 0;
        if (nx + task->frame.width > g_gfx.width)
            nx = g_gfx.width - task->frame.width;
        task_move(task->id, nx, ny);
        x = task->frame.x;
        y = task->frame.y;
        close_bx = x + task->frame.width - close_w;
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

    gfx_fill_rect(x, y, task->frame.width, TASK_TITLE_HEIGHT, CHRIS_TITLE_COLOR);
    if (fill_body) {
        gfx_fill_rect(x, y + TASK_TITLE_HEIGHT, task->frame.width,
                      task->frame.body_height, body_color ? body_color
                                                          : CHRIS_WINDOW_COLOR);
    }
    gfx_fill_rect(x, y, task->frame.width, 1, CHRIS_BORDER_COLOR);
    gfx_fill_rect(x, y + task->frame.body_height - 1, task->frame.width, 1,
                  CHRIS_BORDER_COLOR);
    gfx_fill_rect(x, y, 1, task->frame.body_height, CHRIS_BORDER_COLOR);
    gfx_fill_rect(x + task->frame.width - 1, y, 1, task->frame.body_height,
                  CHRIS_BORDER_COLOR);

    if (title != 0) {
        gfx_draw_text_clipped(font_row, font_arial_width, font_arial_height,
                              title, x + 6, y + 2, CHRIS_TITLE_TEXT,
                              x, y, task->frame.width - close_w * 3,
                              TASK_TITLE_HEIGHT);
    }

    gfx_fill_rect(min_bx, y, close_w, TASK_TITLE_HEIGHT,
                  min_hover ? CHRIS_ACCENT_COLOR : CHRIS_EDITOR_COLOR);
    gfx_draw_text_clipped(font_row, font_arial_width, font_arial_height,
                          "_", min_bx + 6, y + 2,
                          min_hover ? CHRIS_TEXT_COLOR : CHRIS_TITLE_TEXT,
                          min_bx, y, close_w, TASK_TITLE_HEIGHT);
    gfx_fill_rect(max_bx, y, close_w, TASK_TITLE_HEIGHT,
                  max_hover ? CHRIS_ACCENT_COLOR : CHRIS_EDITOR_COLOR);
    gfx_draw_text_clipped(font_row, font_arial_width, font_arial_height,
                          task->window.mode == TASK_WINDOW_MAXIMIZED ? "R" : "M",
                          max_bx + 6, y + 2,
                          max_hover ? CHRIS_TEXT_COLOR : CHRIS_TITLE_TEXT,
                          max_bx, y, close_w, TASK_TITLE_HEIGHT);
    gfx_fill_rect(close_bx, y, close_w, TASK_TITLE_HEIGHT,
                  close_hover ? 0x00E07050u : 0x00804030u);
    gfx_draw_text_clipped(font_row, font_arial_width, font_arial_height,
                          "X", close_bx + 6, y + 2, CHRIS_TITLE_TEXT,
                          close_bx, y, close_w, TASK_TITLE_HEIGHT);
    if (close_hover && input_left_pressed()) {
        input_consume_left_press();
        task_close(task->id);
        return true;
    }
    if (max_hover && input_left_pressed()) {
        if (task->window.mode == TASK_WINDOW_MAXIMIZED) {
            task_restore(task->id);
        } else {
            TaskRect bounds;
            bounds.x = 0;
            bounds.y = UI_TASKBAR_HEIGHT;
            bounds.width = g_gfx.width;
            bounds.body_height = g_gfx.height - UI_TASKBAR_HEIGHT;
            task_maximize(task->id, bounds);
        }
        input_consume_left_press();
    } else if (min_hover && input_left_pressed()) {
        input_consume_left_press();
        task_minimize(task->id);
    }
    return false;
}

bool ui_window(Task *task, uint32_t body_color, const char *title) {
    return ui_window_ex(task, body_color, title, 1);
}

void ui_draw_taskbar(void) {
    static const char *names[] = {"Shell", "Ball", "Edit", "Files", "Tasks"};
    int i;
    int n;
    int x;
    char clock[6];
    unsigned sec;
    unsigned m;
    unsigned s;
    uint64_t now = pit_ticks();

    gfx_fill_rect(0, 0, g_gfx.width, UI_TASKBAR_HEIGHT, CHRIS_TASKBAR_COLOR);
    gfx_fill_rect(0, UI_TASKBAR_HEIGHT - 1, g_gfx.width, 1, CHRIS_ACCENT_COLOR);
    for (i = 0; i < 5; ++i) {
        ui_label(6 + i * 56, 12, 52, 16, names[i], CHRIS_TITLE_TEXT);
    }
    sec = (unsigned)(now / 60ull);
    m = (sec / 60u) % 60u;
    s = sec % 60u;
    clock[0] = (char)('0' + (m / 10u));
    clock[1] = (char)('0' + (m % 10u));
    clock[2] = ':';
    clock[3] = (char)('0' + (s / 10u));
    clock[4] = (char)('0' + (s % 10u));
    clock[5] = 0;
    ui_label(g_gfx.width - 48, 12, 44, 16, clock, CHRIS_TITLE_TEXT);

    n = task_count();
    x = 6 + 5 * 56;
    for (i = 0; i < n && x + 50 < g_gfx.width - 56; ++i) {
        Task *t = task_iter(i);
        if (!t) {
            break;
        }
        gfx_fill_rect(x, 8, 48, 24, CHRIS_EDITOR_COLOR);
        ui_label(x + 2, 12, 44, 16, task_title(t), CHRIS_TITLE_TEXT);
        x += 52;
    }
}

TaskbarAction ui_take_taskbar_action(void) {
    InputMouse mouse = input_mouse_snapshot();
    TaskbarAction action = TASKBAR_NONE;
    int i;
    int n;
    int x;

    if (!input_left_pressed() || mouse.y < 0 || mouse.y >= UI_TASKBAR_HEIGHT) {
        return TASKBAR_NONE;
    }
    if (mouse.x >= 0 && mouse.x < 56) {
        action = TASKBAR_SHELL;
    } else if (mouse.x < 112) {
        action = TASKBAR_BALL;
    } else if (mouse.x < 168) {
        action = TASKBAR_EDITOR;
    } else if (mouse.x < 224) {
        action = TASKBAR_FILES;
    } else if (mouse.x < 280) {
        action = TASKBAR_TASKS;
    } else {
        n = task_count();
        x = 6 + 5 * 56;
        for (i = 0; i < n && x + 50 < g_gfx.width - 56; ++i) {
            Task *t = task_iter(i);
            if (t && ui_hit_rect(mouse.x, mouse.y, x, 8, 48, 24)) {
                task_raise(t->id);
                input_consume_left_press();
                return TASKBAR_NONE;
            }
            x += 52;
        }
        return TASKBAR_NONE;
    }
    input_consume_left_press();
    return action;
}

#define UI_CURSOR_W 12
#define UI_CURSOR_H 12

static uint32_t g_cur_under[UI_CURSOR_W * UI_CURSOR_H];
static int g_cur_x;
static int g_cur_y;
static int g_cur_saved;

static uint32_t cursor_pixel(int x, int y) {
    if (!g_gfx.back || x < 0 || y < 0 || x >= g_gfx.width || y >= g_gfx.height) {
        return 0;
    }
    return g_gfx.back[y * g_gfx.width + x];
}

static void cursor_put(int x, int y, uint32_t color) {
    if (!g_gfx.back || x < 0 || y < 0 || x >= g_gfx.width || y >= g_gfx.height) {
        return;
    }
    g_gfx.back[y * g_gfx.width + x] = color;
}

void ui_undraw_cursor(void) {
    int row;
    int col;
    if (!g_cur_saved) {
        return;
    }
    for (row = 0; row < UI_CURSOR_H; ++row) {
        for (col = 0; col < UI_CURSOR_W; ++col) {
            cursor_put(g_cur_x + col, g_cur_y + row,
                       g_cur_under[row * UI_CURSOR_W + col]);
        }
    }
    gfx_mark_dirty(g_cur_x, g_cur_y, UI_CURSOR_W, UI_CURSOR_H);
    g_cur_saved = 0;
}

void ui_draw_cursor(void) {
    InputMouse mouse = input_mouse_snapshot();
    int row;
    int col;
    int x = mouse.x;
    int y = mouse.y;

    if (vgpu_cursor_active() && vgpu_cursor_move(x, y) == 0) {
        if (g_cur_saved) {
            ui_undraw_cursor();
        }
        return;
    }
    ui_undraw_cursor();
    for (row = 0; row < UI_CURSOR_H; ++row) {
        for (col = 0; col < UI_CURSOR_W; ++col) {
            g_cur_under[row * UI_CURSOR_W + col] = cursor_pixel(x + col, y + row);
        }
    }
    g_cur_x = x;
    g_cur_y = y;
    g_cur_saved = 1;
    gfx_draw_mouse(x, y);
}
