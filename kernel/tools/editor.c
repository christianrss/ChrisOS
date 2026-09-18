/* LEARN:WS64-W06 */
#include "editor.h"

static void ed_clear_line(char *line) {
    int i;
    for (i = 0; i < ED_MAX_COLS; i++) {
        line[i] = 0;
    }
}

static void ed_copy_line(char *dst, const char *src) {
    int i;
    for (i = 0; i < ED_MAX_COLS; i++) {
        dst[i] = src[i];
    }
}

void ed_set_status(Editor *e, const char *msg) {
    int i = 0;
    if (!e) {
        return;
    }
    if (!msg) {
        e->status[0] = 0;
        return;
    }
    while (msg[i] && i < 79) {
        e->status[i] = msg[i];
        i++;
    }
    e->status[i] = 0;
}

void ed_init(Editor *e) {
    int r;
    if (!e) {
        return;
    }
    for (r = 0; r < ED_MAX_LINES; r++) {
        ed_clear_line(e->lines[r]);
    }
    e->nlines = 1;
    e->row = 0;
    e->col = 0;
    e->dirty = 0;
    e->scroll_row = 0;
    e->scroll_col = 0;
    e->name[0] = 0;
    e->status[0] = 0;
}

int ed_length(const Editor *e, int row) {
    int n = 0;
    if (!e || row < 0 || row >= e->nlines) {
        return 0;
    }
    while (n < ED_MAX_COLS - 1 && e->lines[row][n] != 0) {
        n++;
    }
    return n;
}

void ed_set_name(Editor *e, const char *name) {
    int i = 0;
    if (!e) {
        return;
    }
    if (!name) {
        e->name[0] = 0;
        return;
    }
    while (name[i] && i < ED_NAME - 1) {
        e->name[i] = name[i];
        i++;
    }
    e->name[i] = 0;
}

int ed_is_dirty(const Editor *e) {
    if (!e) {
        return 0;
    }
    return e->dirty ? 1 : 0;
}

const char *ed_name(const Editor *e) {
    if (!e) {
        return "";
    }
    return e->name;
}

static void ed_clamp_col(Editor *e) {
    int len = ed_length(e, e->row);
    if (e->col > len) {
        e->col = len;
    }
    if (e->col < 0) {
        e->col = 0;
    }
}

static int ed_insert_char(Editor *e, char ch) {
    int len = ed_length(e, e->row);
    int i;
    if (len >= ED_MAX_COLS - 1 || e->col >= ED_MAX_COLS - 1) {
        ed_set_status(e, "line full");
        return 0;
    }
    for (i = len; i >= e->col; i--) {
        e->lines[e->row][i + 1] = e->lines[e->row][i];
    }
    e->lines[e->row][e->col] = ch;
    e->col++;
    e->dirty = 1;
    return 1;
}

static int ed_enter(Editor *e) {
    int len;
    int i;
    int k;
    if (e->nlines >= ED_MAX_LINES) {
        ed_set_status(e, "too many lines");
        return 0;
    }
    len = ed_length(e, e->row);
    for (i = e->nlines; i > e->row + 1; i--) {
        ed_copy_line(e->lines[i], e->lines[i - 1]);
    }
    e->nlines++;
    ed_clear_line(e->lines[e->row + 1]);
    for (k = e->col; k < len; k++) {
        e->lines[e->row + 1][k - e->col] = e->lines[e->row][k];
        e->lines[e->row][k] = 0;
    }
    e->row++;
    e->col = 0;
    e->dirty = 1;
    return 1;
}

static int ed_join_prev(Editor *e) {
    int prev_len;
    int cur_len;
    int i;
    int k;
    int new_col;
    if (e->row <= 0) {
        return 1;
    }
    prev_len = ed_length(e, e->row - 1);
    cur_len = ed_length(e, e->row);
    if (prev_len + cur_len >= ED_MAX_COLS - 1) {
        ed_set_status(e, "line full");
        return 0;
    }
    for (i = 0; i < cur_len; i++) {
        e->lines[e->row - 1][prev_len + i] = e->lines[e->row][i];
    }
    e->lines[e->row - 1][prev_len + cur_len] = 0;
    new_col = prev_len;
    for (i = e->row; i < e->nlines - 1; i++) {
        for (k = 0; k < ED_MAX_COLS; k++) {
            e->lines[i][k] = e->lines[i + 1][k];
        }
    }
    ed_clear_line(e->lines[e->nlines - 1]);
    e->nlines--;
    e->row--;
    e->col = new_col;
    e->dirty = 1;
    return 1;
}

static int ed_join_next(Editor *e) {
    int cur_len;
    int next_len;
    int i;
    int k;
    if (e->row + 1 >= e->nlines) {
        return 1;
    }
    cur_len = ed_length(e, e->row);
    next_len = ed_length(e, e->row + 1);
    if (cur_len + next_len >= ED_MAX_COLS - 1) {
        ed_set_status(e, "line full");
        return 0;
    }
    for (i = 0; i < next_len; i++) {
        e->lines[e->row][cur_len + i] = e->lines[e->row + 1][i];
    }
    e->lines[e->row][cur_len + next_len] = 0;
    for (i = e->row + 1; i < e->nlines - 1; i++) {
        for (k = 0; k < ED_MAX_COLS; k++) {
            e->lines[i][k] = e->lines[i + 1][k];
        }
    }
    ed_clear_line(e->lines[e->nlines - 1]);
    e->nlines--;
    e->dirty = 1;
    return 1;
}

static int ed_backspace(Editor *e) {
    int len;
    int i;
    if (e->col > 0) {
        len = ed_length(e, e->row);
        for (i = e->col - 1; i < len; i++) {
            e->lines[e->row][i] = e->lines[e->row][i + 1];
        }
        e->col--;
        e->dirty = 1;
        return 1;
    }
    return ed_join_prev(e);
}

static int ed_delete(Editor *e) {
    int len;
    int i;
    len = ed_length(e, e->row);
    if (e->col < len) {
        for (i = e->col; i < len; i++) {
            e->lines[e->row][i] = e->lines[e->row][i + 1];
        }
        e->dirty = 1;
        return 1;
    }
    return ed_join_next(e);
}

int ed_handle(Editor *e, int key) {
    if (!e) {
        return 0;
    }
    e->status[0] = 0;
    if (key == '\n' || key == 10) {
        return ed_enter(e);
    }
    if (key == 8) {
        return ed_backspace(e);
    }
    if (key == 9) {
        int t;
        for (t = 0; t < ED_TAB_SPACES; t++) {
            if (!ed_insert_char(e, ' ')) {
                return 0;
            }
        }
        return 1;
    }
    if (key == ED_LEFT) {
        if (e->col > 0) {
            e->col--;
        }
        return 1;
    }
    if (key == ED_RIGHT) {
        if (e->col < ed_length(e, e->row)) {
            e->col++;
        }
        return 1;
    }
    if (key == ED_UP) {
        if (e->row > 0) {
            e->row--;
            ed_clamp_col(e);
        }
        return 1;
    }
    if (key == ED_DOWN) {
        if (e->row + 1 < e->nlines) {
            e->row++;
            ed_clamp_col(e);
        }
        return 1;
    }
    if (key == ED_HOME) {
        e->col = 0;
        return 1;
    }
    if (key == ED_END) {
        e->col = ed_length(e, e->row);
        return 1;
    }
    if (key == ED_DEL) {
        return ed_delete(e);
    }
    if (key >= 32 && key < 127) {
        return ed_insert_char(e, (char)key);
    }
    return 1;
}

void ed_get_text(const Editor *e, char *out, int out_cap) {
    int r;
    int n = 0;
    if (!out || out_cap <= 0) {
        return;
    }
    out[0] = 0;
    if (!e) {
        return;
    }
    for (r = 0; r < e->nlines; r++) {
        int i = 0;
        while (e->lines[r][i] && n + 2 < out_cap) {
            out[n++] = e->lines[r][i++];
        }
        if (r + 1 < e->nlines && n + 2 < out_cap) {
            out[n++] = '\n';
        }
    }
    out[n] = 0;
}

int ed_load_text(Editor *e, const char *text) {
    int i;
    int row;
    int col;
    if (!e) {
        return 0;
    }
    ed_init(e);
    if (!text) {
        return 1;
    }
    row = 0;
    col = 0;
    for (i = 0; text[i]; i++) {
        if (text[i] == '\n') {
            if (row + 1 >= ED_MAX_LINES) {
                ed_set_status(e, "too many lines");
                e->nlines = row + 1;
                e->row = row;
                e->col = col;
                return 0;
            }
            row++;
            col = 0;
            e->nlines = row + 1;
        } else {
            if (col >= ED_MAX_COLS - 1) {
                ed_set_status(e, "line full");
                e->nlines = row + 1;
                e->row = row;
                e->col = col;
                return 0;
            }
            e->lines[row][col] = text[i];
            col++;
            e->lines[row][col] = 0;
            if (row + 1 > e->nlines) {
                e->nlines = row + 1;
            }
        }
    }
    e->nlines = row + 1;
    e->row = row;
    e->col = col;
    e->dirty = 0;
    e->scroll_row = 0;
    e->scroll_col = 0;
    return 1;
}
