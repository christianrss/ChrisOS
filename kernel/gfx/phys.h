#ifndef CHRIS_PHYS_H
#define CHRIS_PHYS_H

#define PHYS_MAX 16

int phys_add(int x, int y, int w, int h);
void phys_clear(void);
void phys_step(void);
int phys_x(int id);
int phys_y(int id);

#endif
