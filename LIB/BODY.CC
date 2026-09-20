#ifndef BODY_CC_H
#define BODY_CC_H

float body_step_y(float y, float vy, float dt, float g, float radius) {
    y = y + vy * dt;
    if (y < radius)
        y = radius;
    return y;
}

float body_step_vy(float y, float vy, float dt, float g, float radius) {
    vy = vy + g * dt;
    y = y + vy * dt;
    if (y < radius)
        vy = 0.0 - vy * 0.3;
    return vy;
}

int body_hit(float ax, float ay, float az, float ar, float bx, float by,
             float bz, float br) {
    float dx;
    float dy;
    float dz;
    float r;
    dx = ax - bx;
    dy = ay - by;
    dz = az - bz;
    r = ar + br;
    return dx * dx + dy * dy + dz * dz < r * r;
}

#endif
