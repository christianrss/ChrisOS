void snap_store() {
    g_snap.x = g_act[0].x;
    g_snap.y = g_act[0].y;
    g_snap.z = g_act[0].z;
    g_snap.yaw = g_act[0].yaw;
    g_snap.pitch = g_act[0].pitch;
    g_snap.kind = g_act[0].kind;
    g_snap.bits = g_act[0].bits;
}

void snap_load() {
    struct Actor back;
    back = g_snap;
    g_act[0].x = back.x;
    g_act[0].y = back.y;
    g_act[0].z = back.z;
    g_act[0].yaw = back.yaw;
    g_act[0].pitch = back.pitch;
    g_act[0].vx = 0.0;
    g_act[0].vy = 0.0;
    g_act[0].vz = 0.0;
    g_act[0].kind = back.kind;
    g_act[0].bits = back.bits;
}

void note(int c) {
    g_note = c;
    g_toast = 50;
}

void save_pack() {
    int i;
    g_sav[0] = 77;
    g_sav[1] = g_phase;
    g_sav[2] = (int)g_act[0].x;
    g_sav[3] = (int)g_act[0].y;
    g_sav[4] = (int)g_act[0].z;
    g_sav[5] = (int)g_act[0].yaw;
    g_sav[6] = g_wood;
    g_sav[7] = g_pearl;
    g_sav[8] = g_arrows;
    i = 0;
    while (i < 16) {
        g_sav[10 + i] = g_inv[i];
        i = i + 1;
    }
}

int save_write() {
    int fd;
    save_pack();
    snap_store();
    fd = fopen("MINE.SAV");
    if (fd < 0)
        return 0;
    fwrite(fd, g_sav, 40);
    fclose(fd);
    note(1);
    return 1;
}

int save_read() {
    int fd;
    int i;
    fd = fopen("MINE.SAV");
    if (fd < 0)
        return 0;
    fread(fd, g_sav, 40);
    fclose(fd);
    if (g_sav[0] != 77)
        return 0;
    g_phase = g_sav[1];
    g_act[0].x = (float)g_sav[2];
    g_act[0].y = (float)g_sav[3];
    g_act[0].z = (float)g_sav[4];
    g_act[0].yaw = (float)g_sav[5];
    g_wood = g_sav[6];
    g_pearl = g_sav[7];
    g_arrows = g_sav[8];
    i = 0;
    while (i < 16) {
        g_inv[i] = g_sav[10 + i];
        i = i + 1;
    }
    snap_store();
    note(2);
    return 1;
}

void respawn() {
    snap_load();
    note(3);
}
