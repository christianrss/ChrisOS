int main() {
  int hero;
  viewport(320, 200);
  phys_add(0, 180, 320, 20);
  phys_add(40, 140, 24, 24);
  phys_add(80, 100, 24, 24);
  hero = scene_add(1, 20, 0, 40, 0, 4);
  scene_add(1, 70, 0, 50, 0, 2);
  scene_add(1, 20, 0, 80, 0, 3);
  anim_key(0, 20, 0, 40, 0);
  anim_key(8, 48, 0, 60, 15);
  anim_apply(hero, 4);
  phys_step();
  scene_draw(0, 0, 0);
  return 0;
}
