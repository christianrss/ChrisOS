/* LEARN:WS64-W09 */
#include "taskmgr.h"

#include "lang_pipeline.h"
#include "graphics.h"
#include "input.h"
#include "task.h"
#include "ui.h"

static void kill_selected(Task *self) {
    int sel = self->state.taskmgr.selected;
    int ntools = task_count();
    int i;
    int idx = 0;
    if (sel < 0) {
        return;
    }
    for (i = 0; i < ntools; i++) {
        Task *t = task_iter(i);
        if (!t) {
            continue;
        }
        if (idx == sel) {
            if (t->id == self->id) {
                return;
            }
            if (t->type == TASK_APP) {
                lang_kill(t->state.app.lang_slot);
            }
            task_close(t->id);
            return;
        }
        idx++;
    }
    {
        int slot;
        int vm_index = sel - idx;
        int seen = 0;
        for (slot = 0; slot < LANG_VM_SLOTS; slot++) {
            if (!lang_slot_used(slot)) {
                continue;
            }
            if (seen == vm_index) {
                int tid = lang_slot_task(slot);
                lang_kill(slot);
                if (tid >= 0) {
                    task_close(tid);
                }
                return;
            }
            seen++;
        }
    }
}

static void taskmgr_run(Task *task, uint64_t ticks) {
    int x;
    int y;
    int i;
    int row = 0;
    int n;
    InputEvent event;
    (void)ticks;
    if (ui_window(task, CHRIS_WINDOW_COLOR, "Tasks")) {
        return;
    }
    x = task->frame.x + 4;
    y = task->frame.y + TASK_TITLE_HEIGHT + 2;
    if (ui_button(task, x, y, 44, 16, CHRIS_EDITOR_COLOR, "Kill")) {
        kill_selected(task);
    }
    if (ui_button(task, x + 52, y, 44, 16, CHRIS_TASKBAR_COLOR, "Focus")) {
        n = task_count();
        if (task->state.taskmgr.selected >= 0 &&
            task->state.taskmgr.selected < n) {
            Task *t = task_iter(task->state.taskmgr.selected);
            if (t && t->id != task->id) {
                task_raise(t->id);
            }
        }
    }
    n = task_count();
    for (i = 0; i < n; i++) {
        Task *t = task_iter(i);
        char line[48];
        int k = 0;
        int ry;
        const char *kind;
        if (!t) {
            continue;
        }
        if (t->type == TASK_APP) {
            kind = "APP ";
        } else if (t->type == TASK_EDITOR) {
            kind = "ED  ";
        } else if (t->type == TASK_EXPLORER) {
            kind = "FS  ";
        } else if (t->type == TASK_TASKMGR) {
            kind = "TM  ";
        } else {
            kind = "TOOL";
        }
        while (kind[k] && k < 4) {
            line[k] = kind[k];
            k++;
        }
        line[k++] = ' ';
        {
            const char *s = task_title(t);
            int p = 0;
            while (s[p] && k < 46) {
                line[k++] = s[p++];
            }
        }
        line[k] = 0;
        ry = y + 22 + row * 16;
        if (row == task->state.taskmgr.selected) {
            gfx_fill_rect(x, ry, task->frame.width - 12, 16, 0x00C0C0C0u);
        }
        ui_label(x + 2, ry, task->frame.width - 16, 16, line, CHRIS_TEXT_COLOR);
        if (task_is_focused(task) &&
            ui_hit_rect(input_mouse_snapshot().x, input_mouse_snapshot().y,
                        x, ry, task->frame.width - 12, 16) &&
            input_left_pressed()) {
            task->state.taskmgr.selected = row;
            input_consume_left_press();
        }
        row++;
    }
    {
        int slot;
        for (slot = 0; slot < LANG_VM_SLOTS; slot++) {
            char line[48];
            int k = 0;
            int ry;
            const char *nm;
            if (!lang_slot_used(slot)) {
                continue;
            }
            line[k++] = 'V';
            line[k++] = 'M';
            line[k++] = ' ';
            nm = lang_slot_name(slot);
            {
                int p = 0;
                while (nm[p] && k < 46) {
                    line[k++] = nm[p++];
                }
            }
            line[k] = 0;
            ry = y + 22 + row * 16;
            if (row == task->state.taskmgr.selected) {
                gfx_fill_rect(x, ry, task->frame.width - 12, 16, 0x00C0C0C0u);
            }
            ui_label(x + 2, ry, task->frame.width - 16, 16, line,
                     CHRIS_TEXT_COLOR);
            row++;
        }
    }
    if (task_is_focused(task)) {
        while (input_next_event(&event)) {
            if (event.type == INPUT_EVENT_KEY && event.key == INPUT_KEY_UP) {
                if (task->state.taskmgr.selected > 0) {
                    task->state.taskmgr.selected--;
                }
            } else if (event.type == INPUT_EVENT_KEY &&
                       event.key == INPUT_KEY_DOWN) {
                task->state.taskmgr.selected++;
            }
        }
    }
}

void taskmgr_window_open(void) {
    Task *existing = task_find(TASK_TASKMGR);
    TaskRect frame;
    int id;
    if (existing) {
        task_raise(existing->id);
        return;
    }
    frame.x = 80;
    frame.y = UI_TASKBAR_HEIGHT + 40;
    frame.width = 320;
    frame.body_height = 240;
    id = task_spawn(TASK_TASKMGR, frame, taskmgr_run);
    if (id < 0) {
        return;
    }
    (void)id;
}
