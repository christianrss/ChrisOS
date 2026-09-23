struct A {
  int x;
};
struct B {
  int pad;
  int x;
};
int main() {
  A a;
  B b;
  A a2;
  a.x = 1;
  b.pad = 9;
  b.x = 2;
  a2.x = 4;
  return a.x + b.x + a2.x;
}
