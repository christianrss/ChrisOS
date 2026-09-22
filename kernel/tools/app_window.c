/* LEARN:WS64-W08 */
#include "app_window.h"

#include "clvm_sys.h"
#include "font.h"
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

#define APP_CHROME_H 20

static void app_draw_chrome(Task *task, int gw) {
    int x;
    int y;
    int w;
    const char *title;
    if (!task) {
        return;
    }
    x = task->frame.x;
    y = task->frame.y;
    w = gw > 0 ? gw : task->frame.width;
    if (w < 64) {
        w = 64;
    }
    gfx_fill_rect(x, y, w, APP_CHROME_H, 0x00306090u);
    gfx_fill_rect(x + w - 22, y, 22, APP_CHROME_H, 0x00C02828u);
    title = task_title(task);
    gfx_draw_text(font_row, font_arial_width, font_arial_height, title, x + 6,
                  y + 3, 0x00FFFFFFu);
    gfx_draw_text(font_row, font_arial_width, font_arial_height, "X",
                  x + w - 15, y + 3, 0x00FFFFFFu);
}

static void app_run(Task *task, uint64_t ticks) {
    int slot;
    uint32_t *pix;
    int gw;
    int gh;
    int bh;
    int remain;
    int chrome;
    int by;
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
    /* Title chrome only for classic game buffers (not win_begin UI apps). */
    chrome = (gw <= CLVM_SYS_GAME_W + 32 && gh <= CLVM_SYS_GAME_H + 32)
                 ? APP_CHROME_H
                 : 0;
    /* Keep surface width 1:1 with pixel buffer — never stretch. */
    if (task->frame.width != gw) {
        task->frame.width = gw;
    }
    if (task->frame.body_height != gh + chrome) {
        task->frame.body_height = gh + chrome;
    }
    if (chrome > 0) {
        app_draw_chrome(task, gw);
    }
    by = task->frame.y + chrome;
    bh = gh;
    if (by < g_gfx.height) {
        remain = g_gfx.height - by;
        if (bh > remain) {
            bh = remain;
        }
    } else {
        return;
    }
    if (bh < 1) {
        return;
    }
    clvm_sys_blit_to(pix, task->frame.x, by, gw, gh, gw, bh);
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
    if (gw >= g_gfx.width && gh >= g_gfx.height) {
        frame.x = 0;
        frame.y = 0;
        frame.width = gw;
        frame.body_height = gh;
    } else {
        offset = g_window_cascade * 28;
        /* Prefer the right half so Go-spawned games are not buried under Editor. */
        frame.x = g_gfx.width - gw - 48 - (offset % 56);
        if (frame.x < 48) {
            frame.x = 48 + offset;
        }
        frame.y = 48 + offset;
        if (frame.y + gh + APP_CHROME_H > g_gfx.height - 48) {
            frame.y = 48;
        }
        frame.width = gw;
        frame.body_height =
            gh + ((gw <= CLVM_SYS_GAME_W + 32 && gh <= CLVM_SYS_GAME_H + 32)
                      ? APP_CHROME_H
                      : 0);
        ++g_window_cascade;
        if (g_window_cascade > 8) {
            g_window_cascade = 1;
        }
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
    if (!(frame.x == 0 && frame.y == 0 && frame.width >= g_gfx.width)) {
        task_raise(id);
    }
}
