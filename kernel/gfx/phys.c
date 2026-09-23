#include "phys.h"

typedef struct PhysBody {
    int used;
    int x;
    int y;
    int w;
    int h;
    int vx;
    int vy;
} PhysBody;

static PhysBody g_body[PHYS_MAX];

void phys_clear(void) {
    int i;
    for (i = 0; i < PHYS_MAX; ++i)
        g_body[i].used = 0;
}

int phys_add(int x, int y, int w, int h) {
    int i;
    if (w < 1)
        w = 1;
    if (h < 1)
        h = 1;
    for (i = 0; i < PHYS_MAX; ++i) {
        if (g_body[i].used)
            continue;
        g_body[i].used = 1;
        g_body[i].x = x;
        g_body[i].y = y;
        g_body[i].w = w;
        g_body[i].h = h;
        g_body[i].vx = 0;
        g_body[i].vy = 0;
        return i;
    }
    return -1;
}

static int overlap(const PhysBody *a, const PhysBody *b) {
    if (a->x + a->w <= b->x || b->x + b->w <= a->x)
        return 0;
    if (a->y + a->h <= b->y || b->y + b->h <= a->y)
        return 0;
    return 1;
}

void phys_step(void) {
    int i;
    int j;
    for (i = 0; i < PHYS_MAX; ++i) {
        PhysBody *a;
        if (!g_body[i].used)
            continue;
        a = &g_body[i];
        a->vy += 1;
        a->x += a->vx;
        a->y += a->vy;
        if (a->y > 400) {
            a->y = 400;
            a->vy = 0;
        }
        for (j = 0; j < PHYS_MAX; ++j) {
            PhysBody *b;
            int dx;
            int dy;
            if (i == j || !g_body[j].used)
                continue;
            b = &g_body[j];
            if (!overlap(a, b))
                continue;
            dx = (a->x + a->w / 2) - (b->x + b->w / 2);
            dy = (a->y + a->h / 2) - (b->y + b->h / 2);
            if (dx < 0)
                dx = -dx;
            if (dy < 0)
                dy = -dy;
            if (dx > dy) {
                if (a->x < b->x)
                    a->x = b->x - a->w;
                else
                    a->x = b->x + b->w;
                a->vx = 0;
            } else {
                if (a->y < b->y)
                    a->y = b->y - a->h;
                else
                    a->y = b->y + b->h;
                a->vy = 0;
            }
        }
    }
}

int phys_x(int id) {
    if (id < 0 || id >= PHYS_MAX || !g_body[id].used)
        return 0;
    return g_body[id].x;
}

int phys_y(int id) {
    if (id < 0 || id >= PHYS_MAX || !g_body[id].used)
        return 0;
    return g_body[id].y;
}
