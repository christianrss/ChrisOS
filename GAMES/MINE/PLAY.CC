void spawn_player(float x, float y, float z) {
    g_act[0].x = x;
    g_act[0].y = y;
    g_act[0].z = z;
    g_act[0].vx = 0.0;
    g_act[0].vy = 0.0;
    g_act[0].vz = 0.0;
    g_act[0].yaw = 0.0;
    g_act[0].pitch = 0.0;
    g_act[0].kind = 7;
    g_act[0].bits.alive = 1;
    g_act[0].bits.bow = 1;
    snap_store();
}

void boot_actors() {
    int i;
    i = 0;
    while (i < 16) {
        g_act[i].kind = 0;
        g_act[i].bits.alive = 0;
        i = i + 1;
    }
    spawn_player(72.0, 8.2, 80.0);
    g_act[1].kind = 1;
    g_act[1].x = 82.0;
    g_act[1].y = 8.2;
    g_act[1].z = 90.0;
    g_act[1].bits.alive = 1;
    g_act[2].kind = 1;
    g_act[2].x = 86.0;
    g_act[2].y = 8.2;
    g_act[2].z = 84.0;
    g_act[2].bits.alive = 1;
    g_act[3].kind = 2;
    g_act[3].x = 52.0;
    g_act[3].y = 6.2;
    g_act[3].z = 80.0;
    g_act[3].bits.alive = 1;
    g_act[4].kind = 2;
    g_act[4].x = 54.0;
    g_act[4].y = 6.2;
    g_act[4].z = 70.0;
    g_act[4].bits.alive = 1;
    g_act[5].kind = 3;
    g_act[5].x = 68.0;
    g_act[5].y = 12.0;
    g_act[5].z = 90.0;
    g_act[5].bits.alive = 1;
    g_act[6].kind = 3;
    g_act[6].x = 80.0;
    g_act[6].y = 12.0;
    g_act[6].z = 70.0;
    g_act[6].bits.alive = 1;
    g_act[7].kind = 3;
    g_act[7].x = 92.0;
    g_act[7].y = 12.0;
    g_act[7].z = 88.0;
    g_act[7].bits.alive = 1;
    g_act[8].kind = 5;
    g_act[8].x = 69.0;
    g_act[8].y = 8.4;
    g_act[8].z = 91.0;
    g_act[8].state = 4;
    g_act[9].kind = 5;
    g_act[9].x = 81.0;
    g_act[9].y = 8.4;
    g_act[9].z = 71.0;
    g_act[9].state = 4;
    g_act[10].kind = 5;
    g_act[10].x = 93.0;
    g_act[10].y = 8.4;
    g_act[10].z = 89.0;
    g_act[10].state = 4;
    g_act[11].kind = 5;
    g_act[11].x = 53.0;
    g_act[11].y = 6.4;
    g_act[11].z = 76.0;
    g_act[11].state = 15;
    g_act[12].kind = 5;
    g_act[12].x = 51.0;
    g_act[12].y = 6.4;
    g_act[12].z = 66.0;
    g_act[12].state = 15;
    g_phase = 1;
    g_hand = 1;
    g_arrows = 8;
    g_inv[1] = 8;
    g_inv[4] = 6;
    g_wood = 0;
    g_pearl = 0;
}

void take_item(int i) {
    int id;
    id = g_act[i].state;
    if (id < 0 || id > 15)
        id = 1;
    g_inv[id] = g_inv[id] + 1;
    if (id == 4)
        g_wood = g_wood + 1;
    if (id == 15)
        g_pearl = g_pearl + 1;
    g_act[i].kind = 0;
    tone(880, 40);
    note(4);
}

void shoot() {
    int i;
    float scale;
    int slot;
    if (g_arrows < 1) {
        note(5);
        return;
    }
    scale = (float)g_charge / 20.0;
    if (scale < 0.4)
        scale = 0.4;
    if (scale > 2.2)
        scale = 2.2;
    if (scale > 1.6) {
        int bx;
        int by;
        int bz;
        int bid;
        bx = (int)(g_act[0].x + sin(g_act[0].yaw) * 1.8);
        by = (int)(g_act[0].y + 1.0);
        bz = (int)(g_act[0].z - cos(g_act[0].yaw) * 1.8);
        bid = voxel_get(bx, by, bz);
        if (bid == 4 || bid == 10 || bid == 9) {
            putb(bx, by, bz, 0);
            g_inv[bid] = g_inv[bid] + 1;
            tone(180, 20);
        }
    }
    slot = -1;
    i = 1;
    while (i < 16) {
        if (g_act[i].kind == 0) {
            slot = i;
            i = 16;
        }
        i = i + 1;
    }
    if (slot < 0)
        return;
    g_arrows = g_arrows - 1;
    g_act[slot].kind = 4;
    g_act[slot].x = g_act[0].x;
    g_act[slot].y = g_act[0].y + 1.2;
    g_act[slot].z = g_act[0].z;
    g_act[slot].vx = sin(g_act[0].yaw) * scale;
    g_act[slot].vz = 0.0 - cos(g_act[0].yaw) * scale;
    g_act[slot].vy = 0.0 - sin(g_act[0].pitch) * scale;
    g_act[slot].timer = 0;
    g_act[slot].bits.alive = 1;
    tone(220, 30);
    pcm_write(g_pcm, 16);
}

void break_block() {
    int x;
    int y;
    int z;
    int id;
    float reach;
    reach = 2.2;
    x = (int)(g_act[0].x + sin(g_act[0].yaw) * reach);
    y = (int)(g_act[0].y + 1.2 - sin(g_act[0].pitch));
    z = (int)(g_act[0].z - cos(g_act[0].yaw) * reach);
    id = voxel_get(x, y, z);
    if (!sim_solid(id) && id != 5)
        return;
    if (id == 7 && y >= 18)
        return;
    putb(x, y, z, 0);
    if (id > 0 && id < 16)
        g_inv[id] = g_inv[id] + 1;
    g_break = 8;
    tone(140, 20);
    tri(x, y, z, x + 1, y, z, x, y + 1, z, 6);
}

void place_block() {
    int x;
    int y;
    int z;
    int id;
    unsigned slot;
    slot = (unsigned)g_hand;
    if (slot <= 0u || slot > 4u)
        return;
    id = 1;
    if (g_hand == 2)
        id = 3;
    if (g_hand == 3)
        id = 4;
    if (g_hand == 4)
        id = 11;
    if (g_inv[id] < 1) {
        note(6);
        return;
    }
    x = (int)(g_act[0].x + sin(g_act[0].yaw) * 1.6);
    y = (int)(g_act[0].y + 1.0);
    z = (int)(g_act[0].z - cos(g_act[0].yaw) * 1.6);
    if (voxel_get(x, y, z) != 0)
        return;
    putb(x, y, z, id);
    g_inv[id] = g_inv[id] - 1;
}

void play_step() {
    float speed;
    int mx;
    int my;
    int dx;
    int dy;
    int mb;
    int i;
    int id;
    mx = mouse_x();
    my = mouse_y();
    dx = mx - g_pmx;
    dy = my - g_pmy;
    if (dx > 48)
        dx = 48;
    if (dx < -48)
        dx = -48;
    if (dy > 48)
        dy = 48;
    if (dy < -48)
        dy = -48;
    if (g_look && g_mode == 1) {
        g_act[0].yaw = g_act[0].yaw + (float)dx * 0.15;
        g_act[0].pitch = g_act[0].pitch + (float)dy * 0.12;
    }
    if (mx < 12 || mx > 788 || dx == 0) {
        if (key(75))
            g_act[0].yaw = g_act[0].yaw - 2.0;
        if (key(77))
            g_act[0].yaw = g_act[0].yaw + 2.0;
    }
    if (my < 12 || my > 588) {
        if (key(72))
            g_act[0].pitch = g_act[0].pitch - 1.5;
        if (key(80))
            g_act[0].pitch = g_act[0].pitch + 1.5;
    }
    if (g_act[0].pitch > 70.0)
        g_act[0].pitch = 70.0;
    if (g_act[0].pitch < -70.0)
        g_act[0].pitch = -70.0;
    g_pmx = mx;
    g_pmy = my;
    g_look = 1;
    speed = 0.16;
    if (g_mode == 1) {
        if (key(17)) {
            g_act[0].vx = g_act[0].vx + sin(g_act[0].yaw) * speed;
            g_act[0].vz = g_act[0].vz - cos(g_act[0].yaw) * speed;
        }
        if (key(31)) {
            g_act[0].vx = g_act[0].vx - sin(g_act[0].yaw) * speed;
            g_act[0].vz = g_act[0].vz + cos(g_act[0].yaw) * speed;
        }
        if (key(30)) {
            g_act[0].vx = g_act[0].vx - cos(g_act[0].yaw) * speed;
            g_act[0].vz = g_act[0].vz - sin(g_act[0].yaw) * speed;
        }
        if (key(32)) {
            g_act[0].vx = g_act[0].vx + cos(g_act[0].yaw) * speed;
            g_act[0].vz = g_act[0].vz + sin(g_act[0].yaw) * speed;
        }
        if (key(57) && g_act[0].bits.ground)
            g_act[0].vy = 0.42;
    }
    sim_move(&g_act[0].x, &g_act[0].y, &g_act[0].z, &g_act[0].vx, &g_act[0].vy,
             &g_act[0].vz);
    g_act[0].bits.ground = 0;
    if (sim_blocked(g_act[0].x, g_act[0].y - 0.2, g_act[0].z))
        g_act[0].bits.ground = 1;
    id = voxel_get((int)g_act[0].x, (int)g_act[0].y, (int)g_act[0].z);
    g_act[0].bits.water = 0;
    if (id == 5 ||
        voxel_get((int)g_act[0].x, (int)g_act[0].y + 1, (int)g_act[0].z) == 5) {
        g_act[0].bits.water = 1;
        g_act[0].vx = g_act[0].vx * 0.6;
        g_act[0].vz = g_act[0].vz * 0.6;
    }
    if (g_act[0].x < 56.0 && g_act[0].y < 4.5)
        respawn();
    if (id == 7 && g_act[0].y < 16.0)
        save_write();
    if (id == 7 && g_act[0].y >= 16.0)
        g_mode = 5;
    if (g_phase == 1 && g_wood >= 3 && g_act[0].x < 60.0) {
        g_phase = 2;
        g_act[0].x = 59.0;
        g_act[0].y = 8.2;
        g_act[0].z = 78.0;
        note(7);
    }
    if (g_phase == 2 && g_pearl >= 2 && g_act[0].x > 96.0) {
        g_phase = 3;
        g_act[0].x = 94.0;
        g_act[0].y = 8.2;
        g_act[0].z = 70.0;
        note(8);
    }
    if ((ticks() / 8) % 2 == 0)
        ai_step(g_act[0].x, g_act[0].z);
    i = 1;
    while (i < 16) {
        if (g_act[i].kind == 1 || g_act[i].kind == 2 || g_act[i].kind == 3) {
            sim_move(&g_act[i].x, &g_act[i].y, &g_act[i].z, &g_act[i].vx,
                     &g_act[i].vy, &g_act[i].vz);
        }
        if (g_act[i].kind == 5) {
            if (clip_aabb(g_act[0].x, g_act[0].y, g_act[0].z, 1.5, g_act[i].x,
                          g_act[i].y, g_act[i].z, 0.6))
                take_item(i);
        }
        if (g_act[i].kind == 4) {
            g_act[i].vy = g_act[i].vy - 0.02;
            if (hit_sweep(g_act[i].x, g_act[i].y, g_act[i].z, g_act[i].vx,
                          g_act[i].vy, g_act[i].vz, 4) ||
                sim_blocked(g_act[i].x + g_act[i].vx, g_act[i].y,
                            g_act[i].z + g_act[i].vz)) {
                g_act[i].vx = 0.0;
                g_act[i].vy = 0.0;
                g_act[i].vz = 0.0;
                g_act[i].timer = g_act[i].timer + 8;
                tone(90, 16);
            } else {
                g_act[i].x = g_act[i].x + g_act[i].vx;
                g_act[i].y = g_act[i].y + g_act[i].vy;
                g_act[i].z = g_act[i].z + g_act[i].vz;
            }
            g_act[i].timer = g_act[i].timer + 1;
            if (g_act[i].timer > 90)
                g_act[i].kind = 0;
        }
        i = i + 1;
    }
    i = 1;
    while (i < 16) {
        if (g_act[i].kind == 4) {
            int k;
            k = 1;
            while (k < 8) {
                if (g_act[k].kind >= 1 && g_act[k].kind <= 3 &&
                    (body_hit(g_act[i].x, g_act[i].y, g_act[i].z, 0.4,
                              g_act[k].x, g_act[k].y, g_act[k].z, 0.8) ||
                     clip_ray_aabb(g_act[i].x, g_act[i].y, g_act[i].z,
                                   g_act[i].vx, g_act[i].vy, g_act[i].vz,
                                   g_act[k].x, g_act[k].y, g_act[k].z, 1.2))) {
                    g_act[k].state = 2;
                    g_act[k].vx = g_act[i].vx;
                    g_act[i].kind = 0;
                    g_act[8].kind = 5;
                    g_act[8].state = 6;
                    g_act[8].x = g_act[k].x;
                    g_act[8].y = g_act[k].y;
                    g_act[8].z = g_act[k].z;
                    tone(300, 24);
                }
                k = k + 1;
            }
        }
        i = i + 1;
    }
    mb = mouse_btn();
    if (g_mode == 1 && g_hand == 0) {
        if (mb & 2) {
            if (g_charging == 0) {
                g_charging = 1;
                g_charge = 0;
            }
            g_charge = g_charge + 1;
        } else if (g_charging) {
            shoot();
            g_charging = 0;
            g_charge = 0;
        }
    } else if (g_charging) {
        g_charging = 0;
    }
    if (g_mode == 1 && (mb & 1) && g_break == 0)
        break_block();
    if (g_mode == 1 && (mb & 2) && g_hand != 0)
        place_block();
    if (g_mode == 1 && key(33) && g_break == 0)
        break_block();
    if (g_mode == 1 && key(34) && g_break == 0)
        place_block();
    if (key(2))
        g_hand = 0;
    if (key(3))
        g_hand = 1;
    if (key(4))
        g_hand = 2;
    if (key(5))
        g_hand = 3;
    if (key(6))
        g_hand = 4;
    if (key(38))
        sky_flip();
    if (key(63))
        save_write();
    if (key(67))
        save_read();
    if (g_break > 0)
        g_break = g_break - 1;
}
