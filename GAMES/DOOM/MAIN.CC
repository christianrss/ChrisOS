/* Engine entry: init → D_DoomMain (one tick setup) → Tick loop. */

char *dg_argv[6];
char dg_arg0[8];
char dg_arg1[8];
char dg_arg2[32];
char dg_arg3[8];
char dg_arg4[8];

void D_DoomMain();
void doomgeneric_Tick();

void doomgeneric_Create() {
  dg_arg0[0] = 'd';
  dg_arg0[1] = 'o';
  dg_arg0[2] = 'o';
  dg_arg0[3] = 'm';
  dg_arg0[4] = 0;
  dg_arg1[0] = '-';
  dg_arg1[1] = 'i';
  dg_arg1[2] = 'w';
  dg_arg1[3] = 'a';
  dg_arg1[4] = 'd';
  dg_arg1[5] = 0;
  dg_arg2[0] = 'G';
  dg_arg2[1] = 'A';
  dg_arg2[2] = 'M';
  dg_arg2[3] = 'E';
  dg_arg2[4] = 'S';
  dg_arg2[5] = '/';
  dg_arg2[6] = 'D';
  dg_arg2[7] = 'O';
  dg_arg2[8] = 'O';
  dg_arg2[9] = 'M';
  dg_arg2[10] = '/';
  dg_arg2[11] = 'D';
  dg_arg2[12] = 'O';
  dg_arg2[13] = 'O';
  dg_arg2[14] = 'M';
  dg_arg2[15] = '1';
  dg_arg2[16] = '.';
  dg_arg2[17] = 'W';
  dg_arg2[18] = 'A';
  dg_arg2[19] = 'D';
  dg_arg2[20] = 0;
  dg_arg3[0] = '-';
  dg_arg3[1] = 'm';
  dg_arg3[2] = 'b';
  dg_arg3[3] = 0;
  dg_arg4[0] = '1';
  dg_arg4[1] = '6';
  dg_arg4[2] = 0;
  dg_argv[0] = dg_arg0;
  dg_argv[1] = dg_arg1;
  dg_argv[2] = dg_arg2;
  dg_argv[3] = dg_arg3;
  dg_argv[4] = dg_arg4;
  myargc = 5;
  myargv = dg_argv;
  printf("doom argc=%d\n", myargc);
  printf("doom argv0=%s\n", dg_argv[0]);
  printf("doom argv1=%s\n", dg_argv[1]);
  printf("doom argv2=%s\n", dg_argv[2]);
  printf("doom argv2 via my=%s\n", myargv[2]);
  DG_Init();
  D_DoomMain();
}

void main() {
  puts("doom: main");
  puts("Doom Generic 0.1");
  doomgeneric_Create();
  puts("doom: create done");
  while (1) {
    doomgeneric_Tick();
    wait(1);
  }
}
