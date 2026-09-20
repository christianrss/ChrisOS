#include "gfx_fast.h"

#include <emmintrin.h>

void gfx_fast_fill_u32(uint32_t *dst, int count, uint32_t value) {
    int i = 0;
    __m128i v;

    if (dst == 0 || count <= 0)
        return;
    v = _mm_set1_epi32((int)value);
    for (; i + 4 <= count; i += 4)
        _mm_storeu_si128((__m128i *)(dst + i), v);
    for (; i < count; ++i)
        dst[i] = value;
}

void gfx_fast_copy_u32(uint32_t *dst, const uint32_t *src, int count) {
    int i = 0;

    if (dst == 0 || src == 0 || count <= 0)
        return;
    for (; i + 4 <= count; i += 4)
        _mm_storeu_si128((__m128i *)(dst + i),
                         _mm_loadu_si128((const __m128i *)(src + i)));
    for (; i < count; ++i)
        dst[i] = src[i];
}
