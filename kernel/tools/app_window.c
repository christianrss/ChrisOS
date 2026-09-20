/* LEARN:WS64-W08 */
#include "app_window.h"

#include "clvm_sys.h"
#include "graphics.h"
#include "input.h"
#include "lang_pipeline.h"
#include "task.h"
#include "ui.h"

static void app_title_esc(char *dst, const char *name) {
    int i = 0;
    if (!name || !name[0]) {
        name = "App";
    }
    while (name[i] && i < TASK_TITLE_CHARS - 5) {
        dst[i] = name[i];
        i++;
    }
    dst[i++] = ' ';
    dst[i++] = 'E';
    dst[i++] = 's';
    dst[i++] = 'c';
    dst[i] = 0;
}

static int app_wants_quit(Task *task) {
    InputEvent event;
    int quit = 0;

    if (!task_is_focused(task)) {
        return 0;
    }
    if (input_key_down(0x01)) {
        quit = 1;
    }
    while (input_next_event(&event)) {
        if (event.type == INPUT_EVENT_KEY && event.key == INPUT_KEY_ESCAPE) {
            quit = 1;
        }
    }
    return quit;
}

static void app_stop(Task *task, int slot) {
    lang_kill(slot);
    task_close(task->id);
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
    if (app_wants_quit(task)) {
        app_stop(task, slot);
        return;
    }
    if (ui_window_ex(task, 0x00000000u, task_title(task),
                     lang_slot_fullscreen(slot) ? 0 : 1)) {
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
    gw = lang_slot_w(slot);
    gh = lang_slot_h(slot);
    clvm_sys_blit_to(pix,
                     task->frame.x,
                     task->frame.y + TASK_TITLE_HEIGHT,
                     gw,
                     gh,
                     task->frame.width,
                     task->frame.body_height);
}

void app_window_open(int lang_slot, const char *title) {
    TaskRect frame;
    int id;
    Task *task;
    int i;
    char named[TASK_TITLE_CHARS];

    app_title_esc(named, title);
    for (i = 0; i < task_count(); i++) {
        Task *t = task_iter(i);
        if (t && t->type == TASK_APP && t->state.app.lang_slot == lang_slot) {
            task_set_title(t->id, named);
            task_raise(t->id);
            input_clear_events();
            return;
        }
    }
    if (lang_slot_fullscreen(lang_slot)) {
        frame.x = 0;
        frame.y = UI_TASKBAR_HEIGHT;
        frame.width = g_gfx.width;
        frame.body_height = g_gfx.height - UI_TASKBAR_HEIGHT - TASK_TITLE_HEIGHT;
    } else {
        frame.x = 48;
        frame.y = UI_TASKBAR_HEIGHT + 24;
        frame.width = CLVM_SYS_GAME_W + 8;
        frame.body_height = CLVM_SYS_GAME_H;
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
    input_clear_events();
}
