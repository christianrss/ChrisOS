/* LEARN:WS64-W09 */
#include "desktop.h"

#include "cls/cls.h"
#include "fs.h"
#include "graphics.h"
#include "input.h"
#include "lang_pipeline.h"
#include "serial.h"
#include "task.h"
#include "ui.h"

static int boot_one(const char *clv, const char *lst) {
    if (lang_run_path(clv)) {
        return 1;
    }
    serial_puts("boot: compile ");
    serial_puts(lst);
    serial_puts("\n");
    if (!lang_compile_list(lst)) {
        serial_puts("boot: compile failed ");
        serial_puts(lst);
        serial_puts("\n");
        return 0;
    }
    if (!lang_run_path(clv)) {
        serial_puts("boot: run failed ");
        serial_puts(clv);
        serial_puts("\n");
        return 0;
    }
    return 1;
}

void desktop_init(void) {
    task_system_init();
    input_init(g_gfx.width, g_gfx.height);
}

void desktop_boot_apps(void) {
    char err[80];
    char flag[4];
    int n;
    err[0] = 0;
    (void)cls_runtime_load("LIB/WIN.CLS", err, (int)sizeof(err));
    boot_one("APPS/DESKTOP/DESKTOP.CLV", "APPS/DESKTOP/DESKTOP.LST");
    boot_one("APPS/TASKBAR/TASKBAR.CLV", "APPS/TASKBAR/TASKBAR.LST");
    /* Optional headless smoke: SYS/SMOKE.DOOM content starts with '1'. */
    n = fs_read("SYS/SMOKE.DOOM", flag, 1);
    if (n == 1 && flag[0] == '1') {
        serial_puts("boot: SYS/SMOKE.DOOM -> ENGINE.CLV\n");
        boot_one("GAMES/DOOM/ENGINE.CLV", "GAMES/DOOM/ENGINE.LST");
    }
    n = fs_read("SYS/SMOKE.WORLD", flag, 1);
    if (n == 1 && flag[0] == '1') {
        serial_puts("boot: SYS/SMOKE.WORLD -> WORLD.CLV\n");
        boot_one("GAMES/WORLD.CLV", "GAMES/WORLD.LST");
    }
}

void desktop_frame(uint64_t ticks) {
    InputMouse mouse = input_mouse_snapshot();
    InputEvent event;
    int focus;
    Task *focused;
    int slot;

    if (input_left_pressed()) {
        (void)task_focus_at(mouse.x, mouse.y);
    }

    focus = task_focused_id();
    focused = task_get(focus);
    slot = -1;
    if (focused && focused->type == TASK_APP) {
        slot = focused->state.app.lang_slot;
    }
    while (input_next_event(&event)) {
        if (slot < 0) {
            continue;
        }
        if (event.type == INPUT_EVENT_KEY) {
            lang_slot_push_key(slot, (int)event.key);
        } else if (event.type == INPUT_EVENT_TEXT) {
            lang_slot_push_text(slot, (unsigned char)event.character);
        }
    }

    gfx_clear(CHRIS_DESKTOP_COLOR);
    task_run_all(ticks);
    ui_draw_cursor();
}
