void main() {
  int nam;
  int i;
  viewport(160, 40);
  text(4, 8, "ac97", 0x00FFFFFF);
  nam = drv_pci(0, 0, 0, 0);
  i = 0;
  while (i < 8) {
    if (drv_irq(i)) {
      i = 8;
    }
    i = i + 1;
  }
  drv_outw(0, 0);
  while (1) {
  }
}
