/* LEARN:WS64-W02 */
#include "ui.h"

#include "font.h"
#include "graphics.h"
#include "input.h"

static uint32_t dim_color(uint32_t color) {
    uint32_t red = (color >> 16) & 0xFFu;
    uint32_t green = (color >> 8) & 0xFFu;
    uint32_t blue = color & 0xFFu;
    return ((red / 4u) << 16) | ((green / 4u) << 8) | (blue / 4u);
}

bool ui_hit_rect(int px, int py, int x, int y, int width, int height) {
    return width > 0 && height > 0 &&
           px >= x && px < x + width &&
           py >= y && py < y + height;
}

static bool ui_hit_circle(int px, int py, int cx, int cy, int radius) {
    int dx = px - cx;
    int dy = py - cy;
    return dx * dx + dy * dy <= radius * radius;
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
    int text_x = x + 4;
    int text_y = y + (height > 15 ? (height - 15) / 2 : 1);

    gfx_fill_rect(x, y, width, height, hover ? color : dim_color(color));
    if (text != 0) {
        gfx_draw_text_clipped(font_row, font_arial_width, font_arial_height,
                              text, text_x, text_y, 0x00FFFFFFu,
                              x, y, width, height);
    }
    if (hover && input_left_pressed()) {
        input_consume_left_press();
        return true;
    }
    return false;
}

bool ui_icon(int x, int y, uint32_t color, const char *caption) {
    InputMouse mouse = input_mouse_snapshot();
    int tw = UI_ICON_SIZE + 8;
    int th = UI_ICON_SIZE + UI_ICON_TEXT_H;
    bool hover = ui_hit_rect(mouse.x, mouse.y, x, y, tw, th);

    gfx_fill_rect(x, y, UI_ICON_SIZE, UI_ICON_SIZE,
                  hover ? color : dim_color(color));
    if (caption) {
        ui_label(x, y + UI_ICON_SIZE + 1, tw, UI_ICON_TEXT_H,
                 caption, CHRIS_TEXT_COLOR);
    }
    if (hover && input_left_pressed() && mouse.y >= UI_TASKBAR_HEIGHT) {
        input_consume_left_press();
        return true;
    }
    return false;
}

bool ui_window(Task *task, uint32_t body_color, const char *title) {
    InputMouse mouse;
    int x;
    int y;
    int close_x;
    int close_y;
    bool close_hover;
    bool title_hover;

    if (task == 0 || !task->active) {
        return false;
    }

    mouse = input_mouse_snapshot();
    x = task->frame.x;
    y = task->frame.y;
    close_x = x + task->frame.width - 10;
    close_y = y + 10;
    close_hover = task_is_focused(task) &&
                  ui_hit_circle(mouse.x, mouse.y, close_x, close_y, 8);
    title_hover = task_is_focused(task) &&
                  ui_hit_rect(mouse.x, mouse.y, x, y,
                              task->frame.width - 30, TASK_TITLE_HEIGHT);

    if (!mouse.left_down) {
        task->window.dragging = false;
    }

    if (task->window.dragging && mouse.left_down) {
        task->frame.x = mouse.x - task->window.drag_offset_x;
        task->frame.y = mouse.y - task->window.drag_offset_y;
        if (task->frame.y < UI_TASKBAR_HEIGHT) {
            task->frame.y = UI_TASKBAR_HEIGHT;
        }
        x = task->frame.x;
        y = task->frame.y;
        close_x = x + task->frame.width - 10;
        close_y = y + 10;
    } else if (title_hover && input_left_pressed()) {
        task->window.dragging = true;
        task->window.drag_offset_x = mouse.x - x;
        task->window.drag_offset_y = mouse.y - y;
        input_consume_left_press();
    }

    gfx_fill_rect(x, y, task->frame.width, TASK_TITLE_HEIGHT, CHRIS_TITLE_COLOR);
    gfx_fill_rect(x, y + TASK_TITLE_HEIGHT, task->frame.width,
                  task->frame.body_height, body_color);

    if (title != 0) {
        gfx_draw_text_clipped(font_row, font_arial_width, font_arial_height,
                              title, x + 6, y + 2, 0x00FFFFFFu,
                              x, y, task->frame.width - 30, TASK_TITLE_HEIGHT);
    }

    gfx_fill_circle(close_x, close_y, 8,
                    close_hover ? 0x00FF0000u : 0x00400000u);
    if (close_hover && input_left_pressed()) {
        input_consume_left_press();
        task_close(task->id);
        return true;
    }
    return false;
}

void ui_draw_taskbar(void) {
    gfx_fill_rect(0, 0, g_gfx.width, UI_TASKBAR_HEIGHT, CHRIS_TASKBAR_COLOR);
    gfx_fill_rect(0, 0, 50, UI_TASKBAR_HEIGHT, CHRIS_SHELL_COLOR);
    gfx_fill_rect(50, 0, 50, UI_TASKBAR_HEIGHT, CHRIS_BALL_COLOR);
    gfx_fill_rect(100, 0, 70, UI_TASKBAR_HEIGHT, CHRIS_EDITOR_COLOR);
}

TaskbarAction ui_take_taskbar_action(void) {
    InputMouse mouse = input_mouse_snapshot();
    TaskbarAction action = TASKBAR_NONE;

    if (!input_left_pressed() || mouse.y < 0 || mouse.y >= UI_TASKBAR_HEIGHT) {
        return TASKBAR_NONE;
    }
    if (mouse.x >= 0 && mouse.x < 50) {
        action = TASKBAR_SHELL;
    } else if (mouse.x < 100) {
        action = TASKBAR_BALL;
    } else if (mouse.x < 170) {
        action = TASKBAR_EDITOR;
    }
    if (action != TASKBAR_NONE) {
        input_consume_left_press();
    }
    return action;
}

void ui_draw_cursor(void) {
    InputMouse mouse = input_mouse_snapshot();
    gfx_draw_mouse(mouse.x, mouse.y);
}
