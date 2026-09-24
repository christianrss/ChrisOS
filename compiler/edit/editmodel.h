#ifndef CHRIS_EDITMODEL_H
#define CHRIS_EDITMODEL_H

#define EDIT_BUFS 4
#define EDIT_CAP 8192

void edit_reset(void);
int edit_open(const char *name);
int edit_switch(int id);
int edit_active(void);
int edit_close(int id, int force);
int edit_dirty(int id);
int edit_mark_clean(int id);
int edit_length(int id);
int edit_insert(int id, int pos, const char *text);
int edit_delete(int id, int pos, int n);
int edit_undo(int id);
int edit_redo(int id);
int edit_search(int id, const char *pat, int from);
int edit_replace(int id, const char *pat, const char *rep, int all);
int edit_line_offset(int id, int line);
int edit_copy(int id, char *out, int cap);

#endif
