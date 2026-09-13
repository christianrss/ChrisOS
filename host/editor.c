#include "editor.h"

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