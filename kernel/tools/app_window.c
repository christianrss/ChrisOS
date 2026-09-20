/* LEARN:WS64-W08 */
#include "app_window.h"

#include "clvm_sys.h"
#include "graphics.h"
#include "lang_pipeline.h"
#include "task.h"

static int g_window_cascade = 1;

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

static void app_run(Task *task, uint64_t ticks) {
    int slot;
    uint32_t *pix;
    int gw;
    int gh;
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
    clvm_sys_blit_to(pix, task->frame.x, task->frame.y, gw, gh, task->frame.width,
                     task->frame.body_height);
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
    offset = g_window_cascade * 28;
    frame.x = 40 + offset;
    frame.y = 40 + offset;
    frame.width = gw;
    frame.body_height = gh;
    ++g_window_cascade;
    if (g_window_cascade > 8) {
        g_window_cascade = 1;
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
    task_raise(id);
}
