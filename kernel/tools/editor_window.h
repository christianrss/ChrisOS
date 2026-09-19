/* LEARN:WS64-W06 */
#ifndef CHRIS_EDITOR_WINDOW_H
#define CHRIS_EDITOR_WINDOW_H

#include "editor.h"

void editor_window_open(void);
void editor_window_open_path(const char *path);
int ed_open(Editor *e);
int ed_save(Editor *e);

#endif
