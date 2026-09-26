void cube_init() {
    g_v[0] = -0.4;
    g_v[1] = 0.0;
    g_v[2] = -0.4;
    g_v[3] = 0.4;
    g_v[4] = 0.0;
    g_v[5] = -0.4;
    g_v[6] = 0.4;
    g_v[7] = 0.8;
    g_v[8] = -0.4;
    g_v[9] = -0.4;
    g_v[10] = 0.8;
    g_v[11] = -0.4;
    g_v[12] = -0.4;
    g_v[13] = 0.0;
    g_v[14] = 0.4;
    g_v[15] = 0.4;
    g_v[16] = 0.0;
    g_v[17] = 0.4;
    g_v[18] = 0.4;
    g_v[19] = 0.8;
    g_v[20] = 0.4;
    g_v[21] = -0.4;
    g_v[22] = 0.8;
    g_v[23] = 0.4;
    g_mat[0] = 1.0;
    g_mat[5] = 1.0;
    g_mat[10] = 1.0;
    g_mat[15] = 1.0;
    transform(g_v, g_mat, 8);
}

void draw_mob(float x, float y, float z, float yaw, int color) {
    meshf(g_v, 8, 12, x, y, z, yaw, color);
}
