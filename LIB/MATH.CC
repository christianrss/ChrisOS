#include "LIB/MATH.H"

float fabsf(float x) {
    if (x < 0.0) return -x;
    return x;
}

float fabs(float x) {
    return fabsf(x);
}

float tan(float x) {
    float c;
    c = cos(x);
    if (c < 0.0001 && c > -0.0001) {
        if (c < 0.0)
            c = -0.0001;
        else
            c = 0.0001;
    }
    return sin(x) / c;
}

float atan(float x) {
    int sign;
    int inv;
    float z;
    float z2;
    float r;
    sign = 0;
    inv = 0;
    if (x < 0.0) {
        sign = 1;
        x = -x;
    }
    if (x > 1.0) {
        inv = 1;
        x = 1.0 / x;
    }
    z = x;
    z2 = z * z;
    r = z * (1.0 + z2 * (-0.3333333 + z2 * (0.2 + z2 * (-0.142857 + z2 * 0.10186))));
    if (inv)
        r = 1.57079633 - r;
    if (sign)
        r = -r;
    return r;
}

float sqrt(float x) {
    float g;
    int i;
    if (x <= 0.0)
        return 0.0;
    g = x;
    if (g < 1.0)
        g = 1.0;
    i = 0;
    while (i < 12) {
        g = 0.5 * (g + x / g);
        i = i + 1;
    }
    return g;
}
