#include "task.h"

static Task g_tasks[TASK_MAX];
static uint32_t g_next_z;
static int g_focused_id;

static void clear_task(Task *task, int id) {
    int i;
    unsigned char *bytes = (unsigned char *)task;

    for (i = 0; i < (int)sizeof(*task); ++i) {
        bytes[i] = 0;
    }
    task->id = id;
    task->type = TASK_NONE;
    task->title[0] = 0;
}

void task_system_init(void) {
    int i;
    for (i = 0; i < TASK_MAX; ++i) {
        clear_task(&g_tasks[i], i);
    }
    g_next_z = 1;
    g_focused_id = -1;
}

Task *task_get(int task_id) {
    if (task_id < 0 || task_id >= TASK_MAX || !g_tasks[task_id].active) {
        return 0;
    }
    return &g_tasks[task_id];
}

Task *task_find(TaskType type) {
    int i;
    for (i = 0; i < TASK_MAX; ++i) {
        if (g_tasks[i].active && g_tasks[i].type == type) {
            return &g_tasks[i];
        }
    }
    return 0;
}

void task_set_title(int task_id, const char *title) {
    Task *task;
    int i = 0;

    if (task_id < 0 || task_id >= TASK_MAX) {
        return;
    }
    task = &g_tasks[task_id];
    if (!title) {
        task->title[0] = 0;
        return;
    }
    while (title[i] && i < TASK_TITLE_CHARS - 1) {
        task->title[i] = title[i];
        i++;
    }
    task->title[i] = 0;
}

const char *task_title(const Task *task) {
    if (!task || !task->title[0]) {
        return "?";
    }
    return task->title;
}

int task_spawn(TaskType type, TaskRect frame, TaskRunner runner) {
    int i;
    Task *task = 0;

    if (type == TASK_NONE || runner == 0 ||
        frame.width <= 0 || frame.body_height <= 0) {
        return -1;
    }

    for (i = 0; i < TASK_MAX; ++i) {
        if (!g_tasks[i].active) {
            task = &g_tasks[i];
            break;
        }
    }
    if (task == 0) {
        return -1;
    }

    clear_task(task, i);
    task->active = true;
    task->type = type;
    task->frame = frame;
    task->run = runner;
    task->z = g_next_z++;

    if (type == TASK_SHELL) {
        task->state.shell.body_color = 0x00000000u;
        task_set_title(task->id, "Shell");
    } else if (type == TASK_BALL) {
        task->state.ball.center_x = 20;
        task->state.ball.center_y = 30;
        task->state.ball.velocity_x = 5;
        task->state.ball.velocity_y = 5;
        task->state.ball.last_tick = 0;
        task_set_title(task->id, "Ball");
    } else if (type == TASK_EDITOR) {
        task->state.editor.model_slot = 0;
        task->state.editor.scroll_row = 0;
        task->state.editor.scroll_col = 0;
        task_set_title(task->id, "Editor");
    } else if (type == TASK_EXPLORER) {
        task->state.explorer.cwd[0] = 0;
        task->state.explorer.cwd_len = 0;
        task->state.explorer.scroll = 0;
        task->state.explorer.selected = 0;
        task_set_title(task->id, "Files");
    } else if (type == TASK_TASKMGR) {
        task->state.taskmgr.selected = 0;
        task_set_title(task->id, "Tasks");
    } else if (type == TASK_APP) {
        task->state.app.lang_slot = -1;
        task_set_title(task->id, "App");
    }

    g_focused_id = task->id;
    return task->id;
}

void task_close(int task_id) {
    Task *task = task_get(task_id);
    if (task == 0) {
        return;
    }
    clear_task(task, task_id);
    if (g_focused_id == task_id) {
        g_focused_id = -1;
    }
}

void task_raise(int task_id) {
    Task *task = task_get(task_id);
    if (task == 0) {
        return;
    }
    task->z = g_next_z++;
    g_focused_id = task_id;
}

bool task_point_inside(const Task *task, int x, int y) {
    if (task == 0 || !task->active) {
        return false;
    }
    return x >= task->frame.x &&
           x < task->frame.x + task->frame.width &&
           y >= task->frame.y &&
           y < task->frame.y + TASK_TITLE_HEIGHT +
               task->frame.body_height;
}

int task_focus_at(int x, int y) {
    int i;
    int best_id = -1;
    uint32_t best_z = 0;

    for (i = 0; i < TASK_MAX; ++i) {
        Task *task = &g_tasks[i];
        if (task_point_inside(task, x, y) &&
            (best_id < 0 || task->z > best_z)) {
            best_id = i;
            best_z = task->z;
        }
    }

    if (best_id >= 0) {
        task_raise(best_id);
    } else {
        g_focused_id = -1;
    }
    return best_id;
}

int task_focused_id(void) {
    return g_focused_id;
}

bool task_is_focused(const Task *task) {
    return task != 0 && task->active && task->id == g_focused_id;
}

int task_count(void) {
    int i;
    int n = 0;

    for (i = 0; i < TASK_MAX; ++i) {
        if (g_tasks[i].active) {
            n++;
        }
    }
    return n;
}

Task *task_iter(int index) {
    int i;
    int n = 0;

    if (index < 0) {
        return 0;
    }
    for (i = 0; i < TASK_MAX; ++i) {
        if (!g_tasks[i].active) {
            continue;
        }
        if (n == index) {
            return &g_tasks[i];
        }
        n++;
    }
    return 0;
}

void task_run_all(uint64_t ticks) {
    uint32_t after_z = 0;
    int emitted;

    for (emitted = 0; emitted < TASK_MAX; ++emitted) {
        Task *next = 0;
        int i;

        for (i = 0; i < TASK_MAX; ++i) {
            Task *candidate = &g_tasks[i];
            if (!candidate->active || candidate->z <= after_z) {
                continue;
            }
            if (next == 0 || candidate->z < next->z) {
                next = candidate;
            }
        }

        if (next == 0) {
            break;
        }
        after_z = next->z;
        next->run(next, ticks);
    }
}
