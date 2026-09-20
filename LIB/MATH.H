#ifndef MATH_H
#define MATH_H
float sin(float x);
float cos(float x);
float fabsf(float x) {
    if (x < 0.0) return -x;
    return x;
}
#endif
