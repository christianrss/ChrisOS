void DrawWindow(int x, int y, int width, int height, int r, int g, int b) {
    DrawRect(x, y, width, 20, 16, 32, 16);
    DrawRect(x, y + 20, width, height, r, g, b);
}