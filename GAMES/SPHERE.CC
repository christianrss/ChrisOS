void main() {
  float v[351];
  int idx[576];
  int stacks;
  int slices;
  int nv;
  int nt;
  int i;
  int j;
  int k;
  int ti;
  int a0;
  int a1;
  int b0;
  int b1;
  float theta;
  float phi;
  float a;

  stacks = 8;
  slices = 12;
  nv = 117;
  nt = 192;

  k = 0;
  i = 0;
  while (i <= stacks) {
    theta = i * 180.0 / 8.0;
    j = 0;
    while (j <= slices) {
      phi = j * 360.0 / 12.0;
      v[k]   = sin(theta) * cos(phi);
      v[k+1] = cos(theta);
      v[k+2] = sin(theta) * sin(phi);
      k = k + 3;
      j = j + 1;
    }
    i = i + 1;
  }

  ti = 0;
  i = 0;
  while (i < stacks) {
    j = 0;
    while (j < slices) {
      a0 = i * 13 + j;
      a1 = a0 + 1;
      b0 = a0 + 13;
      b1 = b0 + 1;
      idx[ti * 3 + 0] = a0;
      idx[ti * 3 + 1] = b0;
      idx[ti * 3 + 2] = a1;
      ti = ti + 1;
      idx[ti * 3 + 0] = a1;
      idx[ti * 3 + 1] = b0;
      idx[ti * 3 + 2] = b1;
      ti = ti + 1;
      j = j + 1;
    }
    i = i + 1;
  }

  viewport(320, 200);
  a = 0.0;
  while (1) {
    if (key(1)) { return; }
    clear(1);
    cam(0.0, 1.5, 5.0, 0.0, -5.0);
    light(3.0, 6.0, 2.0, 1.0, 1.0, 1.0);
    meshf(0, 117, 192, 0.0, 0.0, 0.0, a, 10);
    a = a + 2.0;
    if (a > 359.0) { a = 0.0; }
    wait(1);
  }
}