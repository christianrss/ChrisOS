#include <stdio.h>

#include "graphics.h"
#include "task.h"

GfxFramebuffer g_gfx;

static int g_runs;
static int g_dirty_marks;

void gfx_mark_dirty(int x, int y, int w, int h) {
    (void)x;
    (void)y;
    (void)w;
    (void)h;
    g_dirty_marks++;
}

void gfx_fill_rect(int x, int y, int width, int height, uint32_t color) {
    (void)color;
    gfx_mark_dirty(x, y, width, height);
}

static void run_task(Task *task, uint64_t ticks) {
    (void)task;
    (void)ticks;
    g_runs++;
}

static int check(int condition, const char *message) {
    if (condition)
        return 1;
    fprintf(stderr, "test_task_window: %s\n", message);
    return 0;
}

int main(void) {
    TaskRect initial = {20, 30, 320, 200};
    TaskRect maximum = {0, 0, 800, 560};
    Task *task;
    int id;

    g_gfx.width = 800;
    g_gfx.height = 600;
    task_system_init();
    id = task_spawn(TASK_APP, initial, run_task);
    if (!check(id >= 0, "spawn failed"))
        return 1;
    task = task_get(id);
    if (!check(task_window_mode(task) == TASK_WINDOW_NORMAL, "not normal"))
        return 1;

    task_minimize(id);
    task_run_all(1);
    if (!check(g_runs == 0, "minimized task rendered"))
        return 1;
    task_raise(id);
    task_run_all(2);
    if (!check(g_runs == 1, "raised task not restored"))
        return 1;

    task_maximize(id, maximum);
    if (!check(task->frame.width == 800 &&
               task_window_mode(task) == TASK_WINDOW_MAXIMIZED,
               "maximize failed"))
        return 1;
    task_restore(id);
    if (!check(task->frame.x == initial.x && task->frame.width == initial.width,
               "restore bounds failed"))
        return 1;

    task_move(id, 40, 50);
    task_resize(id, 400, 240);
    if (!check(task->frame.x == 40 && task->frame.y == 50 &&
               task->frame.width == 400 && task->frame.body_height == 240,
               "move/resize failed"))
        return 1;
    if (!check(g_dirty_marks > 0, "no damage was recorded"))
        return 1;

    {
        TaskRect other = {30, 40, 200, 120};
        int id2 = task_spawn(TASK_APP, other, run_task);
        if (!check(id2 >= 0, "second spawn failed"))
            return 1;
        if (!check(task_id_at(50, 60) == id2, "top window not hit"))
            return 1;
        if (!check(task_id_at(300, 200) == id, "lower window miss"))
            return 1;
    }

    {
        int n;
        int closed = 0;
        for (n = 0; n < 24; ++n) {
            TaskRect box = {10 + n, 10, 80, 40};
            int spawned = task_spawn(TASK_APP, box, run_task);
            if (spawned < 0) {
                break;
            }
            task_close(spawned);
            closed++;
            if (task_get(spawned) != 0 && task_get(spawned)->type != TASK_NONE) {
                fprintf(stderr, "test_task_window: close left a live task\n");
                return 1;
            }
        }
        if (!check(closed == 24, "open/close stress"))
            return 1;
    }

    puts("test_task_window: ok");
    return 0;
}
