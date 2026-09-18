/* LEARN:WS64-W08 */
#include "app_window.h"

#include "clvm_sys.h"
#include "graphics.h"
#include "lang_pipeline.h"
#include "task.h"
#include "ui.h"

static void app_run(Task *task, uint64_t ticks) {
    int slot;
    uint32_t *pix;
    (void)ticks;
    if (!task) {
        return;
    }
    slot = task->state.app.lang_slot;
    if (ui_window(task, 0x00000000u, task_title(task))) {
        lang_kill(slot);
        return;
    }
    if (!lang_slot_used(slot)) {
        task_close(task->id);
        return;
    }
    pix = lang_slot_pixels(slot);
    if (!pix) {
        return;
    }
    clvm_sys_blit_to(pix,
                     task->frame.x,
                     task->frame.y + TASK_TITLE_HEIGHT,
                     task->frame.width,
                     task->frame.body_height);
}

void app_window_open(int lang_slot, const char *title) {
    TaskRect frame;
    int id;
    Task *task;
    int i;
    for (i = 0; i < task_count(); i++) {
        Task *t = task_iter(i);
        if (t && t->type == TASK_APP && t->state.app.lang_slot == lang_slot) {
            task_raise(t->id);
            return;
        }
    }
    frame.x = 48;
    frame.y = UI_TASKBAR_HEIGHT + 24;
    frame.width = CLVM_SYS_GAME_W + 8;
    frame.body_height = CLVM_SYS_GAME_H;
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
    task_set_title(id, title ? title : "App");
}
