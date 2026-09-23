void main() {
  int samples;
  int i;
  int n;
  viewport(160, 60);
  text(8, 8, "wav", 0x00FFFFFF);
  samples = malloc(256);
  i = 0;
  while (i < 128) {
    if (i % 32 < 16) {
      storeb(samples + i * 2, 64);
      storeb(samples + i * 2 + 1, 0);
    }
    i = i + 1;
  }
  n = pcm_write(samples, 128);
  while (1) {
  }
}
