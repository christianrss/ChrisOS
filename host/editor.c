#include "../host/editor.h"

static void ed_clear_line(char *line) {
    int i;
    for (i = 0; i < ED_MAX_COLS; i++) {
        line[i] = 0;
    }
}

void ed_init(Editor *e) {
    int r;
    for (r = 0; r < ED_MAX_LINES; r++) {
        ed_clear_line(e->lines[r]);
    }
    e->nlines = 1;
    e->row = 0;
    e->col = 0;
    e->dirty = 0;
    e->name[0] = 0;
    e->status[0] = 0;
}

int ed_length(const Editor *e, int row) {
    int n = 0;
    if (row < 0 || row >= e->nlines) {
        return 0;
    }
    while (n < ED_MAX_COLS - 1 && e->lines[row][n] != 0) {
        n++;
    }
    return n;
}

static void ed_set_status(Editor *e, const char *msg) {
    int i = 0;
    while (msg[i] && i < 79) {
        e->status[i] = msg[i];
        i++;
    }
    e->status[i] = 0;
}

static int ed_insert_char(Editor *e, char ch) {
    int len = ed_length(e, e->row);
    int i ;
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
    int len, i, k;
    if(e->nlines >= ED_MAX_LINES) {
        ed_set_status(e, "too many lines");
        return 0;
    }
    len = ed_length(e, e->row);
    for (i = e->nlines; i > e->row + 1; i--) {
        for (k = 0; k < ED_MAX_COLS; k++) {
            e->lines[i][k] = e->lines[i - 1][k];
        }
    }
    e->nlines++;
    for (k = 0; k < ED_MAX_COLS; k++) {
        e->lines[e->row + 1][k] = 0;
    }
    for (k = e->col; k < len; k++) {
        e->lines[e->row + 1][k - e->col] = e->lines[e->row][k];
        e->lines[e->row][k] = 0;
    }
    e->row++;
    e->col = 0;
    e->dirty = 1;
    return 1;
}

static void ed_clamp_col(Editor *e) {
    int len = ed_length(e, e->row);
    if (e->col > len) {
        e->col = len;
    }
}

int ed_handle(Editor *e, int key) {
    int len;
    e->status[0] = 0;
    if (key == '\n' || key == 10) {
        return ed_enter(e);
    }
    if (key == 8) {
        if (e->col > 0) {
            int i;
            len = ed_length(e, e->row);
            for (i = e->col - 1; i < len; i++) {
                e->lines[e->row][i] = e->lines[e->row][i + 1];
            }
            e->col--;
            e->dirty = 1;
            return 1;
        }
        return 1;
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
        int i;
        len = ed_length(e, e->row);
        if (e->col < len) {
            for (i = e->col; i < len; i++) {
                e->lines[e->row][i] = e->lines[e->row][i + 1];
            }
            e->dirty = 1;
        }
        return 1;
    }
    if (key >= 32 && key < 127) {
        return ed_insert_char(e, (char)key);
    }
    return 1;
}

void ed_get_text(const Editor *e, char *out, int out_cap) {
    int r, n = 0;
    out[0] = 0;
    for (r = 0; r < e->nlines; r++) {
        int i = 0;
        while (e->lines[r][i] && n + 2 < out_cap) {
            out[n++] = e->lines[r][i++];
        }
        if (r + 1 < e->nlines &&  + 2 < out_cap) {
            out[n++] = '\n';
        }
    }
    out[n] = 0;
}