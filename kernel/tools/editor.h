/* LEARN:DESK64-07 */
#ifndef CHRIS_EDITOR_H
#define CHRIS_EDITOR_H

#define ED_MAX_LINES 256
#define ED_MAX_COLS 128
#define ED_TAB_SPACES 4
#define ED_LEFT  1
#define ED_RIGHT 2
#define ED_UP    3
#define ED_DOWN  4
#define ED_HOME  5
#define ED_END   6
#define ED_DEL   7

typedef struct Editor {
    char lines[ED_MAX_LINES][ED_MAX_COLS];
    int nlines;
    int row;
    int col;
    int dirty;
    char name[32];
    char status[80];
    int scroll_row;
    int scroll_col;
} Editor;

void ed_init(Editor *e);
int ed_length(const Editor *e, int row);
int ed_handle(Editor *e, int key);
void ed_get_text(const Editor *e, char *out, int out_cap);
void ed_set_name(Editor *e, const char *name);
int ed_load_text(Editor *e, const char *text);
void ed_set_status(Editor *e, const char *msg);
int ed_is_dirty(const Editor *e);
const char *ed_name(const Editor *e);

#endif
