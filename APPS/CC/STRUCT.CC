struct Point {
  int x;
  int y;
};

int main() {
  Point p;
  int i;
  int s;
  p.x = 3;
  p.y = 4;
  s = 0;
  for (i = 0; i < 3; i = i + 1) {
    s = s + p.x;
  }
  return s;
}
