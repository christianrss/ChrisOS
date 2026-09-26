void putb(int x, int y, int z, int id) {
    if (x < 1 || z < 1 || y < 1)
        return;
    if (x > 126 || z > 126 || y > 60)
        return;
    voxel(x, y, z, id);
}

void column(int x, int z, int top, int cap) {
    int y;
    y = 5;
    while (y <= top) {
        if (y == top)
            putb(x, y, z, cap);
        else
            putb(x, y, z, 2);
        y = y + 1;
    }
}

void tree_at(int x, int z) {
    int y;
    y = 8;
    while (y <= 11) {
        putb(x, y, z, 4);
        y = y + 1;
    }
    putb(x - 1, 11, z, 9);
    putb(x + 1, 11, z, 9);
    putb(x, 11, z - 1, 9);
    putb(x, 11, z + 1, 9);
    putb(x, 12, z, 9);
}

void gen_world() {
    long row;
    int x;
    int z;
    int y;
    if (g_built)
        return;
    for (row = 52; row < 100; row = row + 1) {
        x = 48;
        while (x < 104) {
            z = (int)row;
            if (x < 58)
                column(x, z, 3, 8);
            else
                column(x, z, 7, 1);
            x = x + 1;
        }
    }
    x = 48;
    while (x < 58) {
        z = 52;
        while (z < 100) {
            putb(x, 4, z, 5);
            putb(x, 5, z, 5);
            putb(x, 6, z, 5);
            putb(x, 7, z, 5);
            z = z + 1;
        }
        x = x + 1;
    }
    z = 64;
    x = 58;
    while (x < 90) {
        putb(x, 8, z, 5);
        putb(x, 8, z + 1, 5);
        x = x + 1;
    }
    column(88, 64, 14, 3);
    column(89, 64, 14, 3);
    y = 8;
    while (y <= 14) {
        putb(87, y, 64, 5);
        putb(87, y, 65, 5);
        y = y + 1;
    }
    x = 64;
    while (x <= 68) {
        z = 72;
        while (z <= 76) {
            putb(x, 7, z, 3);
            z = z + 1;
        }
        x = x + 1;
    }
    putb(66, 7, 74, 5);
    x = 58;
    while (x <= 62) {
        z = 84;
        while (z <= 88) {
            putb(x, 6, z, 3);
            putb(x, 7, z, 5);
            z = z + 1;
        }
        x = x + 1;
    }
    x = 74;
    while (x <= 80) {
        putb(x, 8, 82, 11);
        putb(x, 9, 82, 11);
        putb(x, 10, 82, 11);
        putb(x, 8, 88, 11);
        putb(x, 9, 88, 11);
        putb(x, 10, 88, 11);
        putb(x, 11, 82, 12);
        putb(x, 11, 88, 12);
        x = x + 1;
    }
    z = 83;
    while (z <= 87) {
        putb(74, 8, z, 11);
        putb(74, 9, z, 11);
        putb(80, 8, z, 11);
        putb(80, 9, z, 11);
        putb(80, 10, z, 11);
        z = z + 1;
    }
    putb(76, 7, 85, 7);
    putb(77, 8, 85, 6);
    x = 50;
    while (x <= 60) {
        putb(x, 7, 78, 4);
        putb(x, 7, 79, 13);
        x = x + 1;
    }
    x = 98;
    while (x <= 102) {
        z = 68;
        while (z <= 72) {
            y = 8;
            while (y <= 16) {
                putb(x, y, z, 3);
                y = y + 1;
            }
            z = z + 1;
        }
        x = x + 1;
    }
    putb(100, 17, 70, 3);
    putb(100, 18, 70, 3);
    putb(100, 19, 70, 7);
    tree_at(68, 90);
    tree_at(80, 70);
    tree_at(92, 88);
    putb(70, 8, 86, 10);
    putb(84, 8, 78, 10);
    putb(90, 8, 80, 14);
    putb(96, 8, 74, 15);
    putb(62, 8, 90, 0);
    g_built = 1;
}
