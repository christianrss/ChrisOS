#include "LIB/BODY.CC"
#include "LIB/CLIP.CC"
#include "LIB/ANIM.CC"

void main() {
    float x;
    float y;
    float z;
    float vy;
    float ax;
    int frames;
    x = 0.0;
    y = 8.0;
    z = 0.0;
    vy = 0.0;
    viewport(320, 200);
    frames = 0;
    while (1) {
        if (key(1))
            return;
        vy = body_step_vy(y, vy, 0.05, -20.0, 1.0);
        y = body_step_y(y, vy, 0.05, -20.0, 1.0);
        ax = anim_eval1(0.0, 0.0, 1.0, 2.0, 0.5);
        clear(1);
        meshf(0, 8, 12, x, y, z, frames, 14);
        frames = frames + 2;
        if (frames > 359)
            frames = 0;
        wait(16);
        x = x + clip_aabb(0.0, 0.0, 0.0, 1.0, ax, 0.0, 0.0, 1.0);
        x = x + body_hit(x, y, z, 1.0, ax, 0.0, 0.0, 1.0);
    }
}
