#ifndef CHRIS_EDITOR_H
#define CHRIS_EDITOR_H

#define ED_MAX_LINES 256
#define ED_MAX_COLS 128
#define ED_TAB_SPACES 4

typedef struct Editor {
    char lines[ED_MAX_LINES][ED_MAX_COLS];
    int nlines; /* pelo menos 1 (linha vazia) */
    int row;
    int col;
    int dirty;
    char name[32];
    char status[80];
} Editor;

void ed_init(Editor *e);
int ed_length(const Editor *e, int row);

#endif