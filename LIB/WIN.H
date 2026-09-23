#ifndef WIN_H
#define WIN_H
void win_origin(int x, int y);
int win_begin(int w, int h, int title);
int win_begin_keep(int w, int h, int title);
void win_end();
int win_click(int x, int y, int w, int h);
int win_mouse_in();
int win_ox();
int win_oy();
int win_w();
int win_h();
int win_edge();
int win_vscroll(int x, int y, int h, int max_off, int view, int scroll);
int win_hscroll(int x, int y, int w, int max_off, int view, int scroll);
#endif
