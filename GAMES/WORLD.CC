struct Vec3 {
  float x;
  float y;
  float z;
};

void main() {
  float v[24];
  int idx[36];
  struct Vec3 eye;
  float a;
  int scene;
  int ready;
  int x;
  int z;
  v[0]=-1.0; v[1]=-1.0; v[2]=-1.0;
  v[3]=1.0;  v[4]=-1.0; v[5]=-1.0;
  v[6]=1.0;  v[7]=1.0;  v[8]=-1.0;
  v[9]=-1.0; v[10]=1.0; v[11]=-1.0;
  v[12]=-1.0; v[13]=-1.0; v[14]=1.0;
  v[15]=1.0; v[16]=-1.0; v[17]=1.0;
  v[18]=1.0; v[19]=1.0; v[20]=1.0;
  v[21]=-1.0; v[22]=1.0; v[23]=1.0;
  idx[0]=0;  idx[1]=1;  idx[2]=2;  idx[3]=0;  idx[4]=2;  idx[5]=3;
  idx[6]=4;  idx[7]=6;  idx[8]=5;  idx[9]=4;  idx[10]=7; idx[11]=6;
  idx[12]=0; idx[13]=4; idx[14]=5; idx[15]=0; idx[16]=5; idx[17]=1;
  idx[18]=3; idx[19]=2; idx[20]=6; idx[21]=3; idx[22]=6; idx[23]=7;
  idx[24]=0; idx[25]=3; idx[26]=7; idx[27]=0; idx[28]=7; idx[29]=4;
  idx[30]=1; idx[31]=5; idx[32]=6; idx[33]=1; idx[34]=6; idx[35]=2;
  a=0.0;
  scene=0;
  ready=0;
  eye.x=12.0;
  eye.y=5.0;
  eye.z=22.0;
  viewport(320,200);
  while(1){
    if(key(1)){ return; }
    if(key(2)){ scene=0; }
    if(key(3)){ scene=1; }
    clear(1);
    light(6.0,10.0,8.0,1.0,1.0,1.0);
    if(scene==0){
      draw_cubes(a);
      a=a+1.5;
      if(a>359.0){ a=0.0; }
    } else {
      if(ready==0){
        x=0;
        while(x<24){
          z=0;
          while(z<24){
            voxel(x,0,z,1);
            z=z+1;
          }
          x=x+1;
        }
        voxel(8,1,8,2);
        voxel(9,1,8,2);
        voxel(10,1,8,2);
        voxel(8,2,8,4);
        voxel(12,1,10,3);
        voxel(4,1,16,6);
        ready=1;
      }
      if(key(17)){ eye.z=eye.z-0.25; }
      if(key(31)){ eye.z=eye.z+0.25; }
      if(key(30)){ eye.x=eye.x-0.25; }
      if(key(32)){ eye.x=eye.x+0.25; }
      if(key(72)){ eye.z=eye.z-0.25; }
      if(key(80)){ eye.z=eye.z+0.25; }
      if(key(75)){ eye.x=eye.x-0.25; }
      if(key(77)){ eye.x=eye.x+0.25; }
      if(key(16)){ eye.y=eye.y+0.2; }
      if(key(18)){ eye.y=eye.y-0.2; }
      cam(eye.x,eye.y,eye.z,0.0,0.0);
      world();
    }
    wait(1);
  }
}

void draw_cube(float x, float z, float yaw, int color) {
  meshf(0,8,12,x,0.0,z,yaw,color);
}

void draw_cubes(float yaw) {
  cam(0.0,1.5,6.0,0.0,0.0);
  tex(1);
  draw_cube(-1.8,0.0,yaw,12);
  draw_cube(1.8,0.0,0.0-yaw,10);
  draw_cube(0.0,-2.2,yaw,14);
}
