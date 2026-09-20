#ifndef MATH3D_CC_H
#define MATH3D_CC_H

float v3_dot(float ax, float ay, float az, float bx, float by, float bz) {
    return ax * bx + ay * by + az * bz;
}

float v3_len2(float x, float y, float z) {
    return x * x + y * y + z * z;
}

#endif
