float g_v[24];
int g_idx[36];

void mesh_init() {
  g_v[0] = -1.0;
  g_v[1] = -1.0;
  g_v[2] = -1.0;
  g_v[3] = 1.0;
  g_v[4] = -1.0;
  g_v[5] = -1.0;
  g_v[6] = 1.0;
  g_v[7] = 1.0;
  g_v[8] = -1.0;
  g_v[9] = -1.0;
  g_v[10] = 1.0;
  g_v[11] = -1.0;
  g_v[12] = -1.0;
  g_v[13] = -1.0;
  g_v[14] = 1.0;
  g_v[15] = 1.0;
  g_v[16] = -1.0;
  g_v[17] = 1.0;
  g_v[18] = 1.0;
  g_v[19] = 1.0;
  g_v[20] = 1.0;
  g_v[21] = -1.0;
  g_v[22] = 1.0;
  g_v[23] = 1.0;
  g_idx[0] = 0;
  g_idx[1] = 1;
  g_idx[2] = 2;
  g_idx[3] = 0;
  g_idx[4] = 2;
  g_idx[5] = 3;
  g_idx[6] = 4;
  g_idx[7] = 6;
  g_idx[8] = 5;
  g_idx[9] = 4;
  g_idx[10] = 7;
  g_idx[11] = 6;
  g_idx[12] = 0;
  g_idx[13] = 4;
  g_idx[14] = 5;
  g_idx[15] = 0;
  g_idx[16] = 5;
  g_idx[17] = 1;
  g_idx[18] = 3;
  g_idx[19] = 2;
  g_idx[20] = 6;
  g_idx[21] = 3;
  g_idx[22] = 6;
  g_idx[23] = 7;
  g_idx[24] = 0;
  g_idx[25] = 3;
  g_idx[26] = 7;
  g_idx[27] = 0;
  g_idx[28] = 7;
  g_idx[29] = 4;
  g_idx[30] = 1;
  g_idx[31] = 5;
  g_idx[32] = 6;
  g_idx[33] = 1;
  g_idx[34] = 6;
  g_idx[35] = 2;
}

struct Vec3 {
  float x;
  float y;
  float z;
};

void draw_cube(float x, float z, float yaw, int color) {
  meshf(g_v, 8, 12, x, 0.0, z, yaw, color);
}

void draw_cubes(float yaw) {
  cam(0.0, 1.5, 6.0, 0.0, 0.0);
  light(2.5, 6.0, 4.0, 1.0, 1.0, 1.0);
  draw_cube(-1.8, 0.0, yaw, 12);
  draw_cube(1.8, 0.0, 0.0 - yaw, 10);
  draw_cube(0.0, -2.2, yaw, 14);
}

void main() {
  struct Vec3 eye;
  float a;
  int scene;
  int ready;
  int x;
  int z;
  mesh_init();
  a = 0.0;
  scene = 0;
  ready = 0;
  eye.x = 12.0;
  eye.y = 5.0;
  eye.z = 22.0;
  viewport(320, 200);
  while (1) {
    if (key(1)) {
      return;
    }
    if (key(2)) {
      scene = 0;
    }
    if (key(3)) {
      scene = 1;
    }
    clear(1);
    if (scene == 0) {
      draw_cubes(a);
      a = a + 1.5;
      if (a > 359.0) {
        a = 0.0;
      }
    } else {
      if (ready == 0) {
        x = 0;
        while (x < 24) {
          z = 0;
          while (z < 24) {
            voxel(x, 0, z, 1);
            z = z + 1;
          }
          x = x + 1;
        }
        voxel(8, 1, 8, 2);
        voxel(9, 1, 8, 2);
        voxel(10, 1, 8, 2);
        voxel(8, 2, 8, 4);
        voxel(12, 1, 10, 3);
        voxel(4, 1, 16, 6);
        ready = 1;
      }
      if (key(17)) {
        eye.z = eye.z - 0.25;
      }
      if (key(31)) {
        eye.z = eye.z + 0.25;
      }
      if (key(30)) {
        eye.x = eye.x - 0.25;
      }
      if (key(32)) {
        eye.x = eye.x + 0.25;
      }
      if (key(72)) {
        eye.z = eye.z - 0.25;
      }
      if (key(80)) {
        eye.z = eye.z + 0.25;
      }
      if (key(75)) {
        eye.x = eye.x - 0.25;
      }
      if (key(77)) {
        eye.x = eye.x + 0.25;
      }
      if (key(16)) {
        eye.y = eye.y + 0.2;
      }
      if (key(18)) {
        eye.y = eye.y - 0.2;
      }
      cam(eye.x, eye.y, eye.z, 0.0, 0.0);
      world();
    }
    wait(1);
  }
}
