void main() {
  int st[72];
  int t;
  int cx;
  int cy;
  int pivot;
  int i;
  int j;
  int lx;
  int ly;
  int px;
  int py;
  int r;
  int color;
  int ring;
  int sec_i;
  int min_i;
  int hr_i;
  int pend;

  st[0]=0; st[1]=87; st[2]=173; st[3]=258; st[4]=342; st[5]=422;
  st[6]=499; st[7]=573; st[8]=642; st[9]=707; st[10]=766; st[11]=819;
  st[12]=866; st[13]=906; st[14]=939; st[15]=965; st[16]=984; st[17]=996;
  st[18]=1000; st[19]=996; st[20]=984; st[21]=965; st[22]=939; st[23]=906;
  st[24]=866; st[25]=819; st[26]=766; st[27]=707; st[28]=642; st[29]=573;
  st[30]=499; st[31]=422; st[32]=342; st[33]=258; st[34]=173; st[35]=87;
  st[36]=0; st[37]=0-87; st[38]=0-173; st[39]=0-258; st[40]=0-342; st[41]=0-422;
  st[42]=0-500; st[43]=0-573; st[44]=0-642; st[45]=0-707; st[46]=0-766; st[47]=0-819;
  st[48]=0-866; st[49]=0-906; st[50]=0-939; st[51]=0-965; st[52]=0-984; st[53]=0-996;
  st[54]=0-1000; st[55]=0-996; st[56]=0-984; st[57]=0-965; st[58]=0-939; st[59]=0-906;
  st[60]=0-866; st[61]=0-819; st[62]=0-766; st[63]=0-707; st[64]=0-642; st[65]=0-573;
  st[66]=0-500; st[67]=0-422; st[68]=0-342; st[69]=0-258; st[70]=0-173; st[71]=0-87;

  viewport(320, 200);
  while (1) {
    if (key(1)) { return; }
    t = ticks();
    cx = 160 + st[(t * 2) % 72] * 3 / 1000;
    cy = 88;
    pivot = cy + 58;
    clear(1);
    rect(cx - 4, cy - 68, 8, 14, 6);
    rect(cx - 22, cy - 56, 44, 8, 6);

    ring = 0;
    while (ring < 4) {
      r = 62;
      color = 6;
      if (ring == 1) { r = 58; color = 14; }
      if (ring == 2) { r = 52; color = 15; }
      if (ring == 3) { r = 48; color = 7; }
      lx = cx + st[0] * r / 1000;
      ly = cy - st[18] * r / 1000;
      i = 2;
      while (i < 72) {
        j = i + 18;
        if (j >= 72) { j = j - 72; }
        px = cx + st[i] * r / 1000;
        py = cy - st[j] * r / 1000;
        line(lx, ly, px, py, color);
        lx = px;
        ly = py;
        i = i + 2;
      }
      line(lx, ly, cx + st[0] * r / 1000, cy - st[18] * r / 1000, color);
      ring = ring + 1;
    }

    i = 0;
    while (i < 72) {
      j = i + 18;
      if (j >= 72) { j = j - 72; }
      line(cx + st[i] * 42 / 1000, cy - st[j] * 42 / 1000,
           cx + st[i] * 50 / 1000, cy - st[j] * 50 / 1000, 0);
      i = i + 6;
    }

    sec_i = (t % 3600) * 72 / 3600;
    min_i = (t % 216000) * 72 / 216000;
    hr_i = (t % 2592000) * 72 / 2592000;
    j = hr_i + 18;
    if (j >= 72) { j = j - 72; }
    line(cx, cy, cx + st[hr_i] * 26 / 1000, cy - st[j] * 26 / 1000, 0);
    j = min_i + 18;
    if (j >= 72) { j = j - 72; }
    line(cx, cy, cx + st[min_i] * 38 / 1000, cy - st[j] * 38 / 1000, 0);
    j = sec_i + 18;
    if (j >= 72) { j = j - 72; }
    line(cx, cy, cx + st[sec_i] * 44 / 1000, cy - st[j] * 44 / 1000, 12);
    rect(cx - 2, cy - 2, 4, 4, 0);

    pend = st[(t * 2) % 72] * 8 / 1000;
    if (pend < 0) { pend = 72 + pend; }
    j = pend + 18;
    if (j >= 72) { j = j - 72; }
    px = cx + st[pend] * 44 / 1000;
    py = pivot + st[j] * 44 / 1000;
    line(cx, pivot, px, py, 6);
    rect(px - 5, py - 5, 10, 10, 14);
    wait(1);
  }
}
