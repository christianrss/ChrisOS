/* LEARN:WS64-W09 */
#include "desktop.h"

#include "editor_window.h"
#include "explorer.h"
#include "graphics.h"
#include "input.h"
#include "task.h"
#include "taskmgr.h"
#include "ui.h"

static int g_window_cascade = 1;

static TaskRect cascaded_frame(int width, int body_height) {
    TaskRect frame;
    int offset = g_window_cascade * 40;

    frame.x = offset;
    frame.y = UI_TASKBAR_HEIGHT + offset;
    frame.width = width;
    frame.body_height = body_height;
    ++g_window_cascade;
    if (g_window_cascade > 8) {
        g_window_cascade = 1;
    }
    return frame;
}

static void shell_run(Task *task, uint64_t ticks) {
    int x;
    int y;

    (void)ticks;
    if (ui_window(task, task->state.shell.body_color, "Shell")) {
        return;
    }

    x = task->frame.x;
    y = task->frame.y + TASK_TITLE_HEIGHT;
    if (ui_button(task, x + 20, y, 50, 20,
                  CHRIS_TASKBAR_COLOR, "Dark")) {
        task->state.shell.body_color = 0x00000000u;
    }
    if (ui_button(task, x + 100, y, 50, 20,
                  CHRIS_TASKBAR_COLOR, "Light")) {
        task->state.shell.body_color = CHRIS_WINDOW_COLOR;
    }
}

static void ball_run(Task *task, uint64_t ticks) {
    BallState *ball = &task->state.ball;

    if (ui_window(task, 0x00000000u, "Ball")) {
        return;
    }

    if (ball->last_tick != ticks) {
        ball->last_tick = ticks;
        ball->center_x += ball->velocity_x;
        ball->center_y += ball->velocity_y;

        if (ball->center_x + 10 > task->frame.width ||
            ball->center_x - 10 < 0) {
            ball->velocity_x = -ball->velocity_x;
        }
        if (ball->center_y + 10 >
                task->frame.body_height + TASK_TITLE_HEIGHT - 1 ||
            ball->center_y - 10 < TASK_TITLE_HEIGHT) {
            ball->velocity_y = -ball->velocity_y;
        }
    }

    gfx_fill_circle(task->frame.x + ball->center_x,
                    task->frame.y + ball->center_y,
                    10, CHRIS_TASKBAR_COLOR);
}

static void open_shell(void) {
    (void)task_spawn(TASK_SHELL, cascaded_frame(300, 280), shell_run);
}

static void open_ball(void) {
    (void)task_spawn(TASK_BALL, cascaded_frame(300, 280), ball_run);
}

static void open_demos(void) {
    open_shell();
    open_ball();
}

static void draw_icons(void) {
    int x0 = UI_ICON_GAP;
    int y0 = UI_TASKBAR_HEIGHT + UI_ICON_GAP;
    int stride = UI_ICON_SIZE + UI_ICON_GAP + 24;

    if (ui_icon(x0, y0, CHRIS_EDITOR_COLOR, "Editor")) {
        editor_window_open();
    }
    if (ui_icon(x0 + stride, y0, 0x00008080u, "Files")) {
        explorer_window_open();
    }
    if (ui_icon(x0 + stride * 2, y0, CHRIS_BALL_COLOR, "Demos")) {
        open_demos();
    }
}

void desktop_init(void) {
    task_system_init();
    input_init(g_gfx.width, g_gfx.height);
}

void desktop_frame(uint64_t ticks) {
    InputMouse mouse = input_mouse_snapshot();
    TaskbarAction action = ui_take_taskbar_action();

    if (action == TASKBAR_SHELL) {
        open_shell();
    } else if (action == TASKBAR_BALL) {
        open_ball();
    } else if (action == TASKBAR_EDITOR) {
        editor_window_open();
    } else if (action == TASKBAR_FILES) {
        explorer_window_open();
    } else if (action == TASKBAR_TASKS) {
        taskmgr_window_open();
    } else if (input_left_pressed() && mouse.y >= UI_TASKBAR_HEIGHT) {
        (void)task_focus_at(mouse.x, mouse.y);
    }

    gfx_clear(CHRIS_DESKTOP_COLOR);
    draw_icons();
    task_run_all(ticks);
    ui_draw_taskbar();
    ui_draw_cursor();

    if (input_left_pressed()) {
        input_consume_left_press();
    }
}
