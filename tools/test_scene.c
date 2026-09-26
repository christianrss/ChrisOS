#include <stdio.h>

#include "phys.h"
#include "scene.h"

int main(void) {
    int hero;
    int x, y, z, yaw;
    int box;
    int floor_id;
    if (!scene_in_frustum(0, 0, 0, 0, 40, 8))
        return 1;
    if (scene_in_frustum(0, 0, 0, 0, -40, 8))
        return 2;
    if (scene_in_frustum(0, 0, 0, 200, 10, 4))
        return 3;
    scene_clear();
    hero = scene_add(1, 0, 0, 30, 0, 4);
    scene_add(1, 0, 0, 30, 0, 4);
    scene_add(2, 80, 0, 10, 0, 2);
    if (scene_count() != 3)
        return 4;
    if (scene_visible(0, 0, 0) < 2)
        return 5;
    anim_clear();
    if (anim_key(0, 10, 0, 40, 0) < 0)
        return 6;
    if (anim_key(10, 30, 0, 60, 90) < 0)
        return 7;
    if (!anim_sample(5, &x, &y, &z, &yaw))
        return 8;
    if (x != 20 || z != 50 || yaw != 45)
        return 9;
    anim_apply(hero, 5);
    phys_clear();
    floor_id = phys_add(0, 100, 20, 20);
    box = phys_add(10, 100, 20, 20);
    phys_step();
    if (phys_x(floor_id) == 0 && phys_x(box) == 10)
        return 10;
    printf("scene ok\n");
    return 0;
}
