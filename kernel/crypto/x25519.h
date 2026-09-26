#ifndef CHRIS_X25519_H
#define CHRIS_X25519_H
#include <stdint.h>
void x25519(uint8_t out[32], const uint8_t scalar[32], const uint8_t point[32]);
#endif
