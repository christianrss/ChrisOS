/* LEARN:WS64-W02 */
#ifndef CHRIS_UI_H
#define CHRIS_UI_H

#include <stdbool.h>
#include <stdint.h>

#include "task.h"

#define UI_TASKBAR_HEIGHT 40
#define UI_ICON_SIZE 32
#define UI_ICON_GAP 16
#define UI_ICON_TEXT_H 16

typedef enum {
    TASKBAR_NONE = 0,
    TASKBAR_SHELL,
    TASKBAR_BALL,
    TASKBAR_EDITOR,
    TASKBAR_FILES,
    TASKBAR_TASKS
} TaskbarAction;

bool ui_hit_rect(int px, int py, int x, int y, int width, int height);
void ui_label(int x, int y, int w, int h, const char *text, uint32_t color);
bool ui_button(Task *owner, int x, int y, int width, int height,
               uint32_t color, const char *text);
bool ui_icon(int x, int y, uint32_t color, const char *caption);
bool ui_window(Task *task, uint32_t body_color, const char *title);
bool ui_window_ex(Task *task, uint32_t body_color, const char *title,
                  int fill_body);
void ui_draw_taskbar(void);
TaskbarAction ui_take_taskbar_action(void);
void ui_draw_cursor(void);

#endif
