#ifndef CLIP_CC_H
#define CLIP_CC_H

int clip_aabb(float ax, float ay, float az, float as,
              float bx, float by, float bz, float bs) {
    if (ax + as < bx || bx + bs < ax)
        return 0;
    if (ay + as < by || by + bs < ay)
        return 0;
    if (az + as < bz || bz + bs < az)
        return 0;
    return 1;
}

int clip_ray_aabb(float ox, float oy, float oz,
                  float dx, float dy, float dz,
                  float bx, float by, float bz, float s) {
    float t;
    if (dx == 0.0)
        return clip_aabb(ox, oy, oz, 0.1, bx, by, bz, s);
    t = (bx - ox) / dx;
    if (t < 0.0)
        t = (bx + s - ox) / dx;
    return t >= 0.0 && t < 64.0;
}

#endif
