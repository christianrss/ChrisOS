void ai_step(float px, float pz) {
    long i;
    float dx;
    float dz;
    float step;
    int nx;
    int nz;
    for (i = 1; i < 16; i = i + 1) {
        switch (g_act[i].kind) {
        case 0:
            continue;
        case 4:
            break;
        case 5:
            break;
        default:
            dx = px - g_act[i].x;
            dz = pz - g_act[i].z;
            if (g_act[i].kind == 2) {
                g_act[i].state = 4;
            } else if (g_act[i].kind == 3 && g_rain) {
                g_act[i].state = 1;
            } else if (g_act[i].kind == 3 && dx * dx + dz * dz < 36.0) {
                g_act[i].state = 2;
            } else if (g_phase == 3 && g_act[i].kind == 1 &&
                       dx * dx + dz * dz < 64.0) {
                g_act[i].state = 3;
            } else if (g_act[i].timer % 40 < 20) {
                g_act[i].state = 1;
            } else {
                g_act[i].state = 0;
            }
            step = 0.08;
            if (g_act[i].state == 2)
                step = -0.12;
            if (g_act[i].state == 3)
                step = 0.1;
            if (g_act[i].state == 0)
                step = 0.0;
            if (g_act[i].state == 4)
                step = 0.05;
            nx = (int)(g_act[i].x + step);
            nz = (int)g_act[i].z;
            if (dx < 0.0)
                nx = (int)(g_act[i].x - step);
            if (voxel_get(nx, (int)g_act[i].y, nz) != 0 &&
                voxel_get(nx, (int)g_act[i].y, nz) != 5)
                step = 0.0;
            if (g_act[i].kind == 2 &&
                voxel_get((int)g_act[i].x, (int)g_act[i].y, (int)g_act[i].z) !=
                    5)
                step = 0.0;
            g_act[i].vx = step;
            if (dz > 0.5)
                g_act[i].vz = 0.04;
            else if (dz < -0.5)
                g_act[i].vz = 0.0 - 0.04;
            else
                g_act[i].vz = 0.0;
            if (g_act[i].state == 2)
                g_act[i].vz = 0.0 - g_act[i].vz;
            break;
        }
        g_act[i].timer = g_act[i].timer + 1;
    }
}
