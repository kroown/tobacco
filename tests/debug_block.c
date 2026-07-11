#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "blunt.h"
#include "blunt_simd.h"
#include "../include/blunt_tables.h"

int main(void) {
    /* Encode a single 4x4 block with known values and verify decode */
    const int16_t block_in[16] = {
        2040, -1360, 0, -680,
        0, 0, 0, 0,
        0, 0, 0, 0,
        0, 0, 0, 0
    };

    for (int quality = 80; quality <= 100; quality += 5) {
        int qp = ((100 - quality) * BLUNT_MAX_QP) / 100;
        blunt_init_quant_tables(quality);

        printf("=== Quality %d, QP=%d ===\n", quality, qp);

        /* Print quantizer values */
        printf("  Luma quant[0..5]: ");
        for (int i = 0; i < 6; i++)
            printf("%d ", blunt_luma_quant[qp][i]);
        printf("\n");

        /* Forward WHT */
        int16_t coeff[16];
        blunt_dct4x4_block(block_in, coeff);
        printf("  WHT output: ");
        for (int i = 0; i < 16; i++) printf("%d ", coeff[i]);
        printf("\n");

        /* Quantize */
        int16_t quantized[16];
        for (int i = 0; i < 16; i++) {
            int q = blunt_luma_quant[qp][i];
            if (q == 0) q = 1;
            quantized[i] = (int16_t)((coeff[i] + (q >> 1)) / q);
        }
        printf("  Quantized:  ");
        for (int i = 0; i < 16; i++) printf("%d ", quantized[i]);
        printf("\n");

        /* Zigzag reorder */
        int16_t zigzag[16];
        for (int i = 0; i < 16; i++)
            zigzag[i] = quantized[blunt_zigzag_scan[i]];
        printf("  Zigzag:     ");
        for (int i = 0; i < 16; i++) printf("%d ", zigzag[i]);
        printf("\n");

        /* Dequantize */
        int16_t dequant[16];
        for (int i = 0; i < 16; i++) {
            int lin = blunt_zigzag_scan[i];
            int q = blunt_luma_quant[qp][lin];
            if (q == 0) q = 1;
            dequant[lin] = (int16_t)(zigzag[i] * q);
        }
        printf("  Dequant:    ");
        for (int i = 0; i < 16; i++) printf("%d ", dequant[i]);
        printf("\n");

        /* IDCT */
        int16_t block_out[16];
        memcpy(block_out, dequant, sizeof(dequant));
        blunt_idct4x4_block(block_out);
        printf("  IDCT out:   ");
        for (int i = 0; i < 16; i++) printf("%d ", block_out[i]);
        printf("\n");

        printf("  Expected:   ");
        for (int i = 0; i < 16; i++) printf("%d ", block_in[i]);
        printf("\n\n");
    }
    return 0;
}
