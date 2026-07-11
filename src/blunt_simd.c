#include "blunt.h"
#include "blunt_simd.h"
#include "blunt_tables.h"

#include <immintrin.h>

BluntSimdLevel blunt_simd_detect(void) {
#if defined(__AVX2__)
    return BLUNT_SIMD_AVX2;
#elif defined(__SSE4_1__) || defined(_M_SSE4_1)
    return BLUNT_SIMD_SSE41;
#elif defined(__SSE2__) || defined(_M_X64) || defined(_M_AMD64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
    return BLUNT_SIMD_SSE2;
#else
    return BLUNT_SIMD_NONE;
#endif
}

/* ============================================================
 * Walsh-Hadamard Transform (WHT) 4x4
 *
 * The WHT is its own inverse (up to a scale factor of 1/16).
 * Forward:  y = H * x * H^T       (H is the Hadamard matrix)
 * Inverse:  x = H * y * H^T / 16
 *
 * H = [1  1  1  1]
 *     [1 -1 -1  1]  (reordered for butterfly)
 *     [1 -1  1 -1]
 *     [1  1 -1 -1]
 *
 * Butterfly form:
 *   a = x0 + x1, b = x0 - x1
 *   c = x2 + x3, d = x2 - x3
 *   y0 = a + c,  y1 = a - c,  y2 = b - d,  y3 = b + d
 * ============================================================ */

static void idct4x4_scalar(int16_t *b) {
    /* Inverse WHT on columns (process each column index i) */
    for (int i = 0; i < 4; i++) {
        int s0 = b[0*4+i], s1 = b[1*4+i], s2 = b[2*4+i], s3 = b[3*4+i];
        int a = s0 + s1, c = s2 + s3;
        int b_ = s0 - s1, d = s2 - s3;
        b[0*4+i] = (int16_t)((a + c + 2) >> 2);
        b[1*4+i] = (int16_t)((a - c + 2) >> 2);
        b[2*4+i] = (int16_t)((b_ - d + 2) >> 2);
        b[3*4+i] = (int16_t)((b_ + d + 2) >> 2);
    }
    /* Inverse WHT on rows */
    for (int i = 0; i < 4; i++) {
        int s0 = b[i*4+0], s1 = b[i*4+1], s2 = b[i*4+2], s3 = b[i*4+3];
        int a = s0 + s1, c = s2 + s3;
        int b_ = s0 - s1, d = s2 - s3;
        b[i*4+0] = (int16_t)((a + c + 2) >> 2);
        b[i*4+1] = (int16_t)((a - c + 2) >> 2);
        b[i*4+2] = (int16_t)((b_ - d + 2) >> 2);
        b[i*4+3] = (int16_t)((b_ + d + 2) >> 2);
    }
}

#if defined(__SSE2__) || defined(_M_X64) || defined(_M_AMD64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)

static void motion_comp_16x16_sse2(uint8_t *dst, int ds,
                                    const uint8_t *ref, int rs,
                                    int dx, int dy, int w, int h) {
    if (dx == 0 && dy == 0) {
        for (int y = 0; y < 16; y++) {
            if (y >= h) break;
            int copy_w = 16;
            if (copy_w > w) copy_w = w;
            __m128i v = _mm_loadu_si128((__m128i*)(ref + y * rs));
            _mm_storeu_si128((__m128i*)(dst + y * ds), v);
            if (copy_w > 16) {
                v = _mm_loadl_epi64((__m128i*)(ref + y * rs + 16));
                _mm_storel_epi64((__m128i*)(dst + y * ds + 16), v);
            }
        }
        return;
    }

    int mx = dx & 1, my = dy & 1;
    int bx = dx >> 1, by = dy >> 1;
    (void)my;

    for (int y = 0; y < 16; y++) {
        if (y >= h) break;
        int yy = y + by;
        if (yy + 1 >= h) yy = h - 2;
        if (yy < 0) yy = 0;
        int copy_w = 16;
        if (copy_w > w) copy_w = w;

        const uint8_t *r0 = ref + yy * rs + bx;
        const uint8_t *r1 = r0 + rs;
        uint8_t *d = dst + y * ds;

        int x = 0;
        for (; x + 15 < copy_w; x += 16) {
            __m128i a = _mm_loadu_si128((__m128i*)(r0 + x));
            __m128i bv = _mm_loadu_si128((__m128i*)(r1 + x));
            if (mx) {
                __m128i a2 = _mm_loadu_si128((__m128i*)(r0 + x + 1));
                __m128i b2 = _mm_loadu_si128((__m128i*)(r1 + x + 1));
                a = _mm_avg_epu8(a, a2);
                bv = _mm_avg_epu8(bv, b2);
            }
            __m128i r = _mm_avg_epu8(a, bv);
            _mm_storeu_si128((__m128i*)(d + x), r);
        }
        for (; x < copy_w; x++) {
            int v = r0[x] + r1[x];
            if (mx) v += r0[x+1] + r1[x+1];
            d[x] = (uint8_t)((v + 2) >> 2);
        }
    }
}

static void ycbcr_to_rgb_sse2(const uint8_t *y, const uint8_t *cb,
                               const uint8_t *cr, uint8_t *rgb,
                               int width, int height,
                               int ys, int cbs, int crs, int rs) {
    __m128i v128 = _mm_set1_epi16(128);
    __m128i cr_r = _mm_set1_epi16(179);
    __m128i cb_g = _mm_set1_epi16(44);
    __m128i cr_g = _mm_set1_epi16(91);
    __m128i cb_b = _mm_set1_epi16(227);

    for (int row = 0; row < height; row++) {
        int col = 0;
        int chroma_row = row / 2;
        for (; col + 7 < width; col += 8) {
            __m128i yy = _mm_loadl_epi64((__m128i*)(y + row * ys + col));
            __m128i cc = _mm_loadl_epi64((__m128i*)(cb + chroma_row * cbs + col / 2));
            __m128i dd = _mm_loadl_epi64((__m128i*)(cr + chroma_row * crs + col / 2));

            __m128i yy16 = _mm_unpacklo_epi8(yy, _mm_setzero_si128());
            __m128i cc16 = _mm_sub_epi16(_mm_unpacklo_epi8(cc, _mm_setzero_si128()), v128);
            __m128i dd16 = _mm_sub_epi16(_mm_unpacklo_epi8(dd, _mm_setzero_si128()), v128);

            __m128i r16 = _mm_add_epi16(yy16, _mm_srai_epi16(_mm_mullo_epi16(dd16, cr_r), 8));
            __m128i g16 = _mm_sub_epi16(yy16, _mm_srai_epi16(_mm_mullo_epi16(cc16, cb_g), 8));
            g16 = _mm_sub_epi16(g16, _mm_srai_epi16(_mm_mullo_epi16(dd16, cr_g), 8));
            __m128i b16 = _mm_add_epi16(yy16, _mm_srai_epi16(_mm_mullo_epi16(cc16, cb_b), 8));

            uint8_t tmpr[8], tmpg[8], tmpb[8];
            _mm_storel_epi64((__m128i*)tmpr, _mm_packus_epi16(r16, _mm_setzero_si128()));
            _mm_storel_epi64((__m128i*)tmpg, _mm_packus_epi16(g16, _mm_setzero_si128()));
            _mm_storel_epi64((__m128i*)tmpb, _mm_packus_epi16(b16, _mm_setzero_si128()));

            for (int i = 0; i < 8 && col + i < width; i++) {
                int off = (row * rs + (col + i) * 3);
                rgb[off + 0] = tmpr[i];
                rgb[off + 1] = tmpg[i];
                rgb[off + 2] = tmpb[i];
            }
        }
        for (; col < width; col++) {
            int yy2 = y[row * ys + col];
            int cb2 = cb[chroma_row * cbs + col / 2] - 128;
            int cr2 = cr[chroma_row * crs + col / 2] - 128;
            int off = row * rs + col * 3;
            rgb[off + 0] = (uint8_t)((yy2 + ((cr2 * 179) >> 8)));
            rgb[off + 1] = (uint8_t)((yy2 - ((cb2 * 44 + cr2 * 91) >> 8)));
            rgb[off + 2] = (uint8_t)((yy2 + ((cb2 * 227) >> 8)));
        }
    }
}

#endif

/* ============================================================
 * Public SIMD dispatchers
 * ============================================================ */
void blunt_idct4x4_block(int16_t *block) {
    idct4x4_scalar(block);
}

void blunt_idct4x4_macroblock(int16_t blocks[16][16], int block_count) {
    for (int i = 0; i < block_count; i++)
        blunt_idct4x4_block(blocks[i]);
}

void blunt_dct4x4_block(const int16_t *input, int16_t *output) {
    int16_t tmp[16];
    /* Forward WHT on rows */
    for (int i = 0; i < 4; i++) {
        int s0 = input[i*4+0], s1 = input[i*4+1];
        int s2 = input[i*4+2], s3 = input[i*4+3];
        int a = s0 + s1, c = s2 + s3;
        int b = s0 - s1, d = s2 - s3;
        tmp[i*4+0] = (int16_t)(a + c);
        tmp[i*4+1] = (int16_t)(a - c);
        tmp[i*4+2] = (int16_t)(b - d);
        tmp[i*4+3] = (int16_t)(b + d);
    }
    /* Forward WHT on columns */
    for (int i = 0; i < 4; i++) {
        int s0 = tmp[0*4+i], s1 = tmp[1*4+i];
        int s2 = tmp[2*4+i], s3 = tmp[3*4+i];
        int a = s0 + s1, c = s2 + s3;
        int b = s0 - s1, d = s2 - s3;
        output[0*4+i] = (int16_t)(a + c);
        output[1*4+i] = (int16_t)(a - c);
        output[2*4+i] = (int16_t)(b - d);
        output[3*4+i] = (int16_t)(b + d);
    }
}

void blunt_dct4x4_macroblock(const int16_t blocks[16][16], int16_t out[16][16],
                              int block_count) {
    for (int i = 0; i < block_count; i++)
        blunt_dct4x4_block(blocks[i], out[i]);
}

void blunt_dequantize_block(int16_t *block, int qp) {
    if (qp < 0) qp = 0;
    if (qp > BLUNT_MAX_QP) qp = BLUNT_MAX_QP;
    for (int i = 0; i < 16; i++)
        block[i] *= blunt_luma_quant[qp][i];
}

void blunt_quantize_block(int16_t *block, int qp) {
    if (qp < 0) qp = 0;
    if (qp > BLUNT_MAX_QP) qp = BLUNT_MAX_QP;
    for (int i = 0; i < 16; i++) {
        int q = blunt_luma_quant[qp][i];
        if (q == 0) q = 1;
        block[i] = (int16_t)((block[i] + (q >> 1)) / q);
    }
}

void blunt_ycbcr_to_rgb_plane(const uint8_t *y, const uint8_t *cb,
                               const uint8_t *cr, uint8_t *rgb,
                               int width, int height,
                               int ys, int cbs, int crs, int rs) {
#if defined(__SSE2__) || defined(_M_X64) || defined(_M_AMD64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
    if (blunt_simd_detect() >= BLUNT_SIMD_SSE2) {
        ycbcr_to_rgb_sse2(y, cb, cr, rgb, width, height, ys, cbs, crs, rs);
        return;
    }
#endif
    for (int row = 0; row < height; row++) {
        for (int col = 0; col < width; col++) {
            int yy = y[row * ys + col];
            int chroma_row = row / 2;
            int cbv = cb[chroma_row * cbs + col / 2] - 128;
            int crv = cr[chroma_row * crs + col / 2] - 128;
            int off = row * rs + col * 3;
            rgb[off + 0] = (uint8_t)((yy + ((crv * 179) >> 8)));
            rgb[off + 1] = (uint8_t)((yy - ((cbv * 44 + crv * 91) >> 8)));
            rgb[off + 2] = (uint8_t)((yy + ((cbv * 227) >> 8)));
        }
    }
}

void blunt_rgb_to_ycbcr_plane(const uint8_t *rgb, uint8_t *y,
                               uint8_t *cb, uint8_t *cr,
                               int width, int height,
                               int rs, int ys, int cbs, int crs) {
    for (int row = 0; row < height; row++) {
        for (int col = 0; col < width; col++) {
            int off = row * rs + col * 3;
            int r = rgb[off + 0];
            int g = rgb[off + 1];
            int b = rgb[off + 2];
            y[row * ys + col] = (uint8_t)(((66 * r + 129 * g + 25 * b + 128) >> 8) + 16);
            if ((col & 1) == 0 && (row & 1) == 0) {
                cb[row / 2 * cbs + col / 2] = (uint8_t)(((-38 * r - 74 * g + 112 * b + 128) >> 8) + 128);
                cr[row / 2 * crs + col / 2] = (uint8_t)(((112 * r - 94 * g - 18 * b + 128) >> 8) + 128);
            }
        }
    }
}

void blunt_motion_comp_16x16(uint8_t *dst, int ds,
                              const uint8_t *ref, int rs,
                              int dx, int dy, int width, int height) {
#if defined(__SSE2__) || defined(_M_X64) || defined(_M_AMD64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
    if (blunt_simd_detect() >= BLUNT_SIMD_SSE2) {
        motion_comp_16x16_sse2(dst, ds, ref, rs, dx, dy, width, height);
        return;
    }
#endif
    for (int y = 0; y < 16 && y < height; y++) {
        int sy = y + (dy >> 1);
        if (sy < 0) sy = 0;
        if (sy >= height) sy = height - 1;
        for (int x = 0; x < 16 && x < width; x++) {
            int sx = x + (dx >> 1);
            if (sx < 0) sx = 0;
            if (sx >= width) sx = width - 1;
            dst[y * ds + x] = ref[sy * rs + sx];
        }
    }
}

void blunt_sad_16x16(const uint8_t *a, int as,
                     const uint8_t *b, int bs) {
    (void)a; (void)as; (void)b; (void)bs;
}
