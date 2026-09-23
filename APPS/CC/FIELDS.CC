struct A {
  int x;
};
struct B {
  int pad;
  int x;
};
struct C {
  char c;
  int x;
};
int main() {
  A a;
  B b;
  C c;
  A a2;
  int n;
  a.x = 1;
  b.x = 2;
  c.c = 7;
  c.x = 9;
  a2.x = 4;
  n = a.x + b.x;
  n = n + c.c;
  n = n + c.x;
  n = n + a2.x;
  return n;
}
