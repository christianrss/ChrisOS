#ifndef ANIM_CC_H
#define ANIM_CC_H

float anim_lerp(float a, float b, float u) {
    return a + (b - a) * u;
}

float anim_eval1(float t0, float v0, float t1, float v1, float t) {
    float u;
    float dt;
    dt = t1 - t0;
    if (dt < 0.001)
        u = 0.0;
    else
        u = (t - t0) / dt;
    if (u < 0.0)
        u = 0.0;
    if (u > 1.0)
        u = 1.0;
    return anim_lerp(v0, v1, u);
}

#endif
