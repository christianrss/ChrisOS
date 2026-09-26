#ifndef SIM_CC_H
#define SIM_CC_H

int sim_solid(int id) {
    if (id == 0)
        return 0;
    if (id == 5)
        return 0;
    return 1;
}

int sim_blocked(float x, float y, float z) {
    int ix;
    int iy;
    int iz;
    ix = (int)x;
    iy = (int)y;
    iz = (int)z;
    if (sim_solid(voxel_get(ix, iy, iz)))
        return 1;
    if (sim_solid(voxel_get(ix, iy + 1, iz)))
        return 1;
    return 0;
}

void sim_move(float *x, float *y, float *z, float *vx, float *vy, float *vz) {
    float nx;
    float ny;
    float nz;
    int step;
    *vy = *vy - 0.045;
    nx = *x + *vx;
    if (sim_blocked(nx, *y, *z))
        *vx = 0.0;
    else
        *x = nx;
    nz = *z + *vz;
    if (sim_blocked(*x, *y, nz))
        *vz = 0.0;
    else
        *z = nz;
    ny = *y + *vy;
    if (sim_blocked(*x, ny, *z)) {
        if (*vy < 0.0)
            *vy = 0.0;
        else
            *vy = 0.0 - *vy * 0.2;
    } else {
        *y = ny;
    }
    if (*y < 1.0) {
        *y = 1.0;
        *vy = 0.0;
    }
    step = 0;
    while (step < 12 && sim_blocked(*x, *y, *z)) {
        *y = *y + 1.0;
        *vy = 0.0;
        step = step + 1;
    }
    *vx = *vx * 0.72;
    *vz = *vz * 0.72;
}

#endif
