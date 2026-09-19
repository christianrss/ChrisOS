/* LEARN:WS64-W03 */
#ifndef CHRIS_TASK_H
#define CHRIS_TASK_H

#include <stdbool.h>
#include <stdint.h>

#define TASK_MAX 32
#define TASK_TITLE_HEIGHT 20
#define TASK_TITLE_CHARS 24

typedef struct {
    int x;
    int y;
    int width;
    int body_height;
} TaskRect;

typedef enum {
    TASK_NONE = 0,
    TASK_SHELL,
    TASK_BALL,
    TASK_EDITOR,
    TASK_EXPLORER,
    TASK_TASKMGR,
    TASK_APP
} TaskType;

typedef struct {
    bool dragging;
    int drag_offset_x;
    int drag_offset_y;
} WindowState;

typedef struct {
    uint32_t body_color;
} ShellState;

typedef struct {
    int center_x;
    int center_y;
    int velocity_x;
    int velocity_y;
    uint64_t last_tick;
} BallState;

typedef struct {
    int model_slot;
    int scroll_row;
    int scroll_col;
} EditorTaskState;

typedef struct {
    int lang_slot;
} AppTaskState;

typedef struct {
    int cwd_len;
    char cwd[512];
    int scroll;
    int selected;
} ExplorerState;

typedef struct {
    int selected;
} TaskmgrState;

struct Task;
typedef void (*TaskRunner)(struct Task *task, uint64_t ticks);

typedef struct Task {
    int id;
    bool active;
    uint32_t z;
    TaskType type;
    TaskRect frame;
    WindowState window;
    TaskRunner run;
    char title[TASK_TITLE_CHARS];
    union {
        ShellState shell;
        BallState ball;
        EditorTaskState editor;
        AppTaskState app;
        ExplorerState explorer;
        TaskmgrState taskmgr;
    } state;
} Task;

void task_system_init(void);
int task_spawn(TaskType type, TaskRect frame, TaskRunner runner);
void task_set_title(int task_id, const char *title);
const char *task_title(const Task *task);
int task_count(void);
Task *task_iter(int index);
Task *task_get(int task_id);
Task *task_find(TaskType type);
void task_close(int task_id);
void task_raise(int task_id);
int task_focus_at(int x, int y);
int task_focused_id(void);
bool task_is_focused(const Task *task);
bool task_point_inside(const Task *task, int x, int y);
void task_run_all(uint64_t ticks);

#endif
