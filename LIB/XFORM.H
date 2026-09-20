#ifndef XFORM_H
#define XFORM_H

struct Xform {
    float x;
    float y;
    float z;
    float yaw;
    float pitch;
    int parent;
};

void xform_ident(struct Xform *t) {
    t->x = 0.0;
    t->y = 0.0;
    t->z = 0.0;
    t->yaw = 0.0;
    t->pitch = 0.0;
    t->parent = -1;
}

void xform_world(struct Xform *t, float *ox, float *oy, float *oz) {
    *ox = t->x;
    *oy = t->y;
    *oz = t->z;
}

#endif
