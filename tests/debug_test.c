#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "blunt.h"
#include "blunt_tables.h"
#include "blunt_simd.h"

#define FLUSH fflush(stdout)

int main(void) {
    printf("=== Minimal Block Roundtrip ===\n"); FLUSH;
    blunt_init_quant_tables(80);

    int qp = (80 * BLUNT_MAX_QP) / 100;
    printf("qp=%d, Q[0]=%d, Q[1]=%d\n", qp,
           blunt_luma_quant[qp][0], blunt_luma_quant[qp][1]); FLUSH;

    int16_t block[16];
    for (int i = 0; i < 16; i++) block[i] = 128;

    int16_t coeff[16];
    blunt_dct4x4_block(block, coeff);
    printf("Forward WHT: "); for (int i=0;i<4;i++) printf("%d ", coeff[i]); printf("\n"); FLUSH;

    /* Quantize */
    for (int i = 0; i < 16; i++) {
        int q = blunt_luma_quant[qp][i];
        if (q == 0) q = 1;
        coeff[i] = (int16_t)((coeff[i] + (q >> 1)) / q);
    }
    printf("Quantized: "); for (int i=0;i<4;i++) printf("%d ", coeff[i]); printf("\n"); FLUSH;

    /* Dequantize */
    for (int i = 0; i < 16; i++)
        coeff[i] *= blunt_luma_quant[qp][i];
    printf("Dequantized: "); for (int i=0;i<4;i++) printf("%d ", coeff[i]); printf("\n"); FLUSH;

    /* IDCT */
    blunt_idct4x4_block(coeff);
    printf("After IDCT: "); for (int i=0;i<4;i++) printf("%d ", coeff[i]); printf("\n"); FLUSH;

    /* Encode/decode single macroblock via bitstream */
    printf("\n=== Encode/Decode single macroblock via bitstream ===\n"); FLUSH;

    uint8_t bitbuf[256];
    memset(bitbuf, 0, sizeof(bitbuf));

    size_t byte_pos = 0;
    int bit_pos = 0;

    int16_t block2[16];
    for (int i = 0; i < 16; i++) block2[i] = 128;

    int16_t wcoeff[16];
    blunt_dct4x4_block(block2, wcoeff);

    int16_t qcoeff[16];
    for (int i = 0; i < 16; i++) {
        int q = blunt_luma_quant[qp][i];
        if (q == 0) q = 1;
        qcoeff[i] = (int16_t)((wcoeff[i] + (q >> 1)) / q);
    }

    int16_t zz[16];
    for (int i = 0; i < 16; i++)
        zz[i] = qcoeff[blunt_zigzag_scan[i]];

    printf("Zigzag: "); for (int i=0;i<16;i++) printf("%d ", zz[i]); printf("\n"); FLUSH;

    int idx = 0;
    while (idx < 16) {
        if (zz[idx] == 0) { idx++; continue; }
        int run = 0;
        while (idx + run < 16 && zz[idx + run] == 0) run++;
        if (idx + run >= 16) break;
        int level = zz[idx + run];
        int abs_level = level < 0 ? -level : level;
        int sign = level < 0 ? 1 : 0;

        printf("  Writing: run=%d level=%d (abs=%d)\n", run, level, abs_level); FLUSH;

        if (abs_level <= 14) {
            /* 8 bits */
            uint8_t val = (uint8_t)((run << 4) | abs_level);
            for (int b = 7; b >= 0; b--) {
                bitbuf[byte_pos] |= ((val >> b) & 1) << (7 - bit_pos);
                bit_pos++;
                if (bit_pos >= 8) { bit_pos = 0; byte_pos++; }
            }
            /* 1 bit sign */
            bitbuf[byte_pos] |= (sign << (7 - bit_pos));
            bit_pos++;
            if (bit_pos >= 8) { bit_pos = 0; byte_pos++; }
        } else {
            /* Escape: 8 bits + 12 bits */
            uint8_t hdr = (uint8_t)((run << 4) | 0x0F);
            for (int b = 7; b >= 0; b--) {
                bitbuf[byte_pos] |= ((hdr >> b) & 1) << (7 - bit_pos);
                bit_pos++;
                if (bit_pos >= 8) { bit_pos = 0; byte_pos++; }
            }
            uint16_t raw12 = (uint16_t)(level & 0xFFF);
            for (int b = 11; b >= 0; b--) {
                bitbuf[byte_pos] |= ((raw12 >> b) & 1) << (7 - bit_pos);
                bit_pos++;
                if (bit_pos >= 8) { bit_pos = 0; byte_pos++; }
            }
        }
        idx = idx + run + 1;
    }
    /* End of block */
    uint8_t eob = 0x00;
    for (int b = 7; b >= 0; b--) {
        bitbuf[byte_pos] |= ((eob >> b) & 1) << (7 - bit_pos);
        bit_pos++;
        if (bit_pos >= 8) { bit_pos = 0; byte_pos++; }
    }
    if (bit_pos > 0) byte_pos++;

    printf("Bitstream: %zu bytes\n", byte_pos); FLUSH;
    for (size_t i = 0; i < byte_pos && i < 20; i++)
        printf("%02X ", bitbuf[i]);
    printf("\n"); FLUSH;

    size_t read_pos = 0;
    int read_bit = 0;

    #define BR_READ(n) ({ \
        uint32_t val = 0; \
        for (int _i = 0; _i < (n); _i++) { \
            if (read_pos >= byte_pos) break; \
            val <<= 1; \
            val |= (bitbuf[read_pos] >> (7 - read_bit)) & 1; \
            read_bit++; \
            if (read_bit >= 8) { read_bit = 0; read_pos++; } \
        } \
        val; \
    })

    int16_t decoded[16];
    memset(decoded, 0, sizeof(decoded));
    idx = 0;
    while (idx < 16) {
        uint32_t rl = BR_READ(8);
        int run = (rl >> 4) & 0xF;
        int level = rl & 0xF;
        if (level == 0 && run == 0) { printf("  EOB at idx=%d\n", idx); break; }
        if (level == 0xF && run == 0) {
            int32_t raw = BR_READ(12);
            if (raw >= 2048) raw -= 4096;
            idx += run;
            printf("  Escape: run=%d raw=%d -> idx=%d\n", run, raw, idx); FLUSH;
            if (idx < 16) decoded[blunt_dezigzag_scan[idx]] = (int16_t)raw;
            idx++;
        } else {
            int sign = BR_READ(1);
            int val = level;
            if (sign) val = -val;
            idx += run;
            printf("  Normal: run=%d val=%d -> idx=%d\n", run, val, idx); FLUSH;
            if (idx < 16) decoded[blunt_dezigzag_scan[idx]] = (int16_t)val;
            idx++;
        }
    }

    printf("Decoded (pre-dequant): "); for(int i=0;i<4;i++) printf("%d ", decoded[i]); printf("\n"); FLUSH;

    /* Dequantize */
    for (int i = 0; i < 16; i++)
        decoded[i] *= blunt_luma_quant[qp][i];

    printf("Dequantized: "); for(int i=0;i<4;i++) printf("%d ", decoded[i]); printf("\n"); FLUSH;

    /* IDCT */
    blunt_idct4x4_block(decoded);
    printf("After IDCT: "); for(int i=0;i<4;i++) printf("%d ", decoded[i]); printf("\n"); FLUSH;

    return 0;
}
