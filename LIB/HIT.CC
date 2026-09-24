#ifndef HIT_CC_H
#define HIT_CC_H

int hit_solid_at(float x, float y, float z) {
    return sim_solid(voxel_get((int)x, (int)y, (int)z));
}

int hit_sweep(float x, float y, float z, float dx, float dy, float dz, int n) {
    int i;
    float t;
    float u;
    if (n < 1)
        n = 1;
    i = 1;
    while (i <= n) {
        t = (float)i;
        u = (float)n;
        if (hit_solid_at(x + dx * t / u, y + dy * t / u, z + dz * t / u))
            return i;
        i = i + 1;
    }
    return 0;
}

#endif
