#ifndef BLUNT_SIMD_H
#define BLUNT_SIMD_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BLUNT_SIMD_NONE = 0,
    BLUNT_SIMD_SSE2,
    BLUNT_SIMD_SSE41,
    BLUNT_SIMD_AVX2,
} BluntSimdLevel;

BluntSimdLevel blunt_simd_detect(void);

void blunt_idct4x4_block(int16_t *block);
void blunt_idct4x4_macroblock(int16_t blocks[16][16], int block_count);

void blunt_dct4x4_block(const int16_t *input, int16_t *output);
void blunt_dct4x4_macroblock(const int16_t blocks[16][16], int16_t out[16][16],
                             int block_count);

void blunt_dequantize_block(int16_t *block, int qp);
void blunt_quantize_block(int16_t *block, int qp);

void blunt_ycbcr_to_rgb_plane(const uint8_t *y, const uint8_t *cb,
                              const uint8_t *cr, uint8_t *rgb,
                              int width, int height,
                              int y_stride, int cb_stride, int cr_stride,
                              int rgb_stride);
void blunt_rgb_to_ycbcr_plane(const uint8_t *rgb, uint8_t *y,
                              uint8_t *cb, uint8_t *cr,
                              int width, int height,
                              int rgb_stride, int y_stride,
                              int cb_stride, int cr_stride);

void blunt_motion_comp_16x16(uint8_t *dst, int dst_stride,
                              const uint8_t *ref, int ref_stride,
                              int dx, int dy, int width, int height);

void blunt_sad_16x16(const uint8_t *a, int a_stride,
                     const uint8_t *b, int b_stride);

#ifdef __cplusplus
}
#endif

#endif
