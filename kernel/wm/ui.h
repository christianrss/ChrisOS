#ifndef CHRIS_UI_H
#define CHRIS_UI_H

#include <stdbool.h>
#include <stdint.h>

#include "task.h"

typedef enum {
    TASKBAR_NONE = 0,
    TASKBAR_SHELL,
    TASKBAR_BALL,
    TASKBAR_EDITOR
} TaskbarAction;

bool ui_hit_rect(int px, int py, int x, int y, int width, int height);
bool ui_button(Task *owner, int x, int y, int width, int height,
               uint32_t color, const char *text);
bool ui_window(Task *task, uint32_t body_color, const char *title);
void ui_draw_taskbar(void);
TaskbarAction ui_take_taskbar_action(void);
void ui_draw_cursor(void);

#endif
