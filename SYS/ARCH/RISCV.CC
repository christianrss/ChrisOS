void main() {
  viewport(280, 64);
  text(8, 8, "riscv64", 0x00FFFFFF);
  text(8, 28, "sv39 4k", 0x00E0E0E0);
  text(8, 44, "no kernel", 0x00FFC080);
  while (1) {
    wait(400);
  }
}
