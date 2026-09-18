/* LEARN:WS64-W06 */
#include "explorer.h"

#include "cfs.h"
#include "editor_window.h"
#include "fs.h"
#include "graphics.h"
#include "input.h"
#include "task.h"
#include "ui.h"

#define EXP_MAX 64
#define EXP_ROW_H 16

typedef struct ExpEntry {
    char name[CFS_NAME_MAX + 1];
    uint32_t size;
    uint16_t type;
} ExpEntry;

static ExpEntry g_ents[EXP_MAX];
static int g_nent;
static char g_status[80];

static void exp_copy(char *dst, int cap, const char *src) {
    int i = 0;
    if (cap < 1) {
        return;
    }
    if (!src) {
        dst[0] = 0;
        return;
    }
    while (src[i] && i < cap - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = 0;
}

static void exp_status(const char *msg) {
    exp_copy(g_status, 80, msg);
}

static int join_path(const char *cwd, const char *name, char *out, int cap) {
    int i = 0;
    int j = 0;
    if (!out || cap < 2) {
        return 0;
    }
    if (cwd && cwd[0]) {
        while (cwd[i] && i < cap - 1) {
            out[i] = cwd[i];
            i++;
        }
        if (i < cap - 1) {
            out[i++] = '/';
        }
    }
    while (name && name[j] && i < cap - 1) {
        out[i++] = name[j++];
    }
    out[i] = 0;
    return i > 0;
}

static int parent_path(const char *cwd, char *out, int cap) {
    int n = 0;
    int slash = -1;
    int i;
    if (!cwd || !cwd[0]) {
        if (out && cap > 0) {
            out[0] = 0;
        }
        return 0;
    }
    while (cwd[n]) {
        if (cwd[n] == '/') {
            slash = n;
        }
        n++;
    }
    if (slash < 0) {
        if (out && cap > 0) {
            out[0] = 0;
        }
        return 1;
    }
    if (!out || cap < 2) {
        return 0;
    }
    for (i = 0; i < slash && i < cap - 1; i++) {
        out[i] = cwd[i];
    }
    out[i] = 0;
    return 1;
}

static int list_cb(void *ctx, const char *name, uint32_t size, uint16_t type) {
    int *n = ctx;
    uint32_t i = 0u;
    if (*n >= EXP_MAX) {
        return 0;
    }
    while (name[i] && i < CFS_NAME_MAX) {
        g_ents[*n].name[i] = name[i];
        i++;
    }
    g_ents[*n].name[i] = 0;
    g_ents[*n].size = size;
    g_ents[*n].type = type;
    (*n)++;
    return 0;
}

static void exp_reload(Task *task) {
    int rc;
    g_nent = 0;
    rc = fs_list_at(task->state.explorer.cwd, list_cb, &g_nent);
    if (rc < 0) {
        g_nent = 0;
        exp_status("list failed");
        return;
    }
    if (task->state.explorer.selected >= g_nent) {
        task->state.explorer.selected = g_nent ? g_nent - 1 : 0;
    }
    exp_status(task->state.explorer.cwd[0] ? task->state.explorer.cwd : "/");
}

static int is_openable(const char *name) {
    int n = 0;
    while (name[n]) {
        n++;
    }
    if (n >= 3) {
        if (name[n - 3] == '.' && name[n - 2] == 'C' && name[n - 1] == 'C') {
            return 1;
        }
    }
    if (n >= 4) {
        if (name[n - 4] == '.' &&
            ((name[n - 3] == 'C' && name[n - 2] == 'V' && name[n - 1] == 'A') ||
             (name[n - 3] == 'C' && name[n - 2] == 'L' && name[n - 1] == 'V') ||
             (name[n - 3] == 'T' && name[n - 2] == 'X' && name[n - 1] == 'T'))) {
            return 1;
        }
    }
    return 0;
}

static void exp_open_selected(Task *task) {
    ExplorerState *ex = &task->state.explorer;
    char path[FS_PATH];
    if (ex->selected < 0 || ex->selected >= g_nent) {
        return;
    }
    if (!join_path(ex->cwd, g_ents[ex->selected].name, path, FS_PATH)) {
        exp_status("path too long");
        return;
    }
    if (g_ents[ex->selected].type == CFS_INODE_DIR) {
        exp_copy(ex->cwd, 96, path);
        ex->cwd_len = 0;
        while (ex->cwd[ex->cwd_len]) {
            ex->cwd_len++;
        }
        ex->selected = 0;
        ex->scroll = 0;
        exp_reload(task);
        return;
    }
    if (!is_openable(g_ents[ex->selected].name)) {
        exp_status("not a text/app file");
        return;
    }
    editor_window_open_path(path);
}

static void exp_up(Task *task) {
    char parent[96];
    if (!parent_path(task->state.explorer.cwd, parent, 96)) {
        return;
    }
    exp_copy(task->state.explorer.cwd, 96, parent);
    task->state.explorer.cwd_len = 0;
    while (task->state.explorer.cwd[task->state.explorer.cwd_len]) {
        task->state.explorer.cwd_len++;
    }
    task->state.explorer.selected = 0;
    task->state.explorer.scroll = 0;
    exp_reload(task);
}

static void exp_new_folder(Task *task) {
    char path[FS_PATH];
    int rc;
    if (!join_path(task->state.explorer.cwd, "NEWDIR", path, FS_PATH)) {
        exp_status("path too long");
        return;
    }
    rc = fs_mkdir(path);
    if (rc == CFS_EEXIST) {
        exp_status("NEWDIR exists");
        return;
    }
    if (rc != CFS_OK) {
        exp_status("mkdir failed");
        return;
    }
    exp_reload(task);
    exp_status("folder created");
}

static void exp_delete_selected(Task *task) {
    ExplorerState *ex = &task->state.explorer;
    char path[FS_PATH];
    int rc;
    if (ex->selected < 0 || ex->selected >= g_nent) {
        return;
    }
    if (!join_path(ex->cwd, g_ents[ex->selected].name, path, FS_PATH)) {
        return;
    }
    if (g_ents[ex->selected].type == CFS_INODE_DIR) {
        rc = fs_rmdir(path);
    } else {
        rc = fs_unlink(path);
    }
    if (rc != CFS_OK) {
        exp_status("delete failed");
        return;
    }
    exp_reload(task);
    exp_status("deleted");
}

static void explorer_run(Task *task, uint64_t ticks) {
    ExplorerState *ex;
    int x;
    int y;
    int i;
    int rows;
    int bx;
    InputEvent event;
    (void)ticks;
    if (!task) {
        return;
    }
    ex = &task->state.explorer;
    if (ui_window(task, CHRIS_WINDOW_COLOR, "Files")) {
        return;
    }
    x = task->frame.x;
    y = task->frame.y + TASK_TITLE_HEIGHT;
    bx = x + 4;
    if (ui_button(task, bx, y + 2, 36, 16, CHRIS_TASKBAR_COLOR, "Up")) {
        exp_up(task);
    }
    if (ui_button(task, bx + 40, y + 2, 70, 16, CHRIS_TASKBAR_COLOR, "NewDir")) {
        exp_new_folder(task);
    }
    if (ui_button(task, bx + 114, y + 2, 44, 16, CHRIS_TASKBAR_COLOR, "Open")) {
        exp_open_selected(task);
    }
    if (ui_button(task, bx + 162, y + 2, 50, 16, CHRIS_TASKBAR_COLOR, "Delete")) {
        exp_delete_selected(task);
    }
    ui_label(x + 4, y + 22, task->frame.width - 8, 16,
             g_status[0] ? g_status : "/", CHRIS_TEXT_COLOR);
    rows = (task->frame.body_height - 60) / EXP_ROW_H;
    if (rows < 1) {
        rows = 1;
    }
    if (ex->selected < ex->scroll) {
        ex->scroll = ex->selected;
    }
    if (ex->selected >= ex->scroll + rows) {
        ex->scroll = ex->selected - rows + 1;
    }
    if (task_is_focused(task)) {
        while (input_next_event(&event)) {
            if (event.type == INPUT_EVENT_KEY && event.key == INPUT_KEY_UP) {
                if (ex->selected > 0) {
                    ex->selected--;
                }
            } else if (event.type == INPUT_EVENT_KEY &&
                       event.key == INPUT_KEY_DOWN) {
                if (ex->selected + 1 < g_nent) {
                    ex->selected++;
                }
            } else if (event.type == INPUT_EVENT_KEY &&
                       event.key == INPUT_KEY_ENTER) {
                exp_open_selected(task);
            }
        }
    }
    for (i = 0; i < rows; i++) {
        int idx = ex->scroll + i;
        int ry = y + 40 + i * EXP_ROW_H;
        char line[40];
        int k;
        if (idx >= g_nent) {
            break;
        }
        if (idx == ex->selected) {
            gfx_fill_rect(x + 4, ry, task->frame.width - 8, EXP_ROW_H,
                          0x00C0C0C0u);
        }
        k = 0;
        if (g_ents[idx].type == CFS_INODE_DIR) {
            line[k++] = '[';
            line[k++] = 'D';
            line[k++] = ']';
            line[k++] = ' ';
        } else {
            line[k++] = '[';
            line[k++] = 'F';
            line[k++] = ']';
            line[k++] = ' ';
        }
        {
            int s = 0;
            while (g_ents[idx].name[s] && k < 38) {
                line[k++] = g_ents[idx].name[s++];
            }
        }
        line[k] = 0;
        ui_label(x + 6, ry, task->frame.width - 12, EXP_ROW_H, line,
                 CHRIS_TEXT_COLOR);
        if (task_is_focused(task) &&
            ui_hit_rect(input_mouse_snapshot().x, input_mouse_snapshot().y,
                        x + 4, ry, task->frame.width - 8, EXP_ROW_H) &&
            input_left_pressed()) {
            ex->selected = idx;
            input_consume_left_press();
            exp_open_selected(task);
        }
    }
}

static void explorer_spawn(const char *path) {
    Task *existing = task_find(TASK_EXPLORER);
    TaskRect frame;
    int id;
    Task *task;
    if (existing) {
        if (path) {
            exp_copy(existing->state.explorer.cwd, 96, path);
        }
        existing->state.explorer.selected = 0;
        existing->state.explorer.scroll = 0;
        task_raise(existing->id);
        exp_reload(existing);
        return;
    }
    frame.x = 60;
    frame.y = UI_TASKBAR_HEIGHT + 20;
    frame.width = 360;
    frame.body_height = 280;
    id = task_spawn(TASK_EXPLORER, frame, explorer_run);
    if (id < 0) {
        return;
    }
    task = task_get(id);
    if (!task) {
        return;
    }
    exp_copy(task->state.explorer.cwd, 96, path ? path : "");
    task->state.explorer.cwd_len = 0;
    while (task->state.explorer.cwd[task->state.explorer.cwd_len]) {
        task->state.explorer.cwd_len++;
    }
    exp_reload(task);
}

void explorer_window_open(void) {
    explorer_spawn("");
}

void explorer_window_open_at(const char *path) {
    explorer_spawn(path);
}
