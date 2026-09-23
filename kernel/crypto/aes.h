#ifndef CHRIS_AES_H
#define CHRIS_AES_H
#include <stdint.h>
void aes128_encrypt(const uint8_t key[16], const uint8_t in[16], uint8_t out[16]);
#endif
