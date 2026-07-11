#include "blunt_audio.h"
#include "blunt_tables.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static int count_nz(const int32_t *block) {
    int n = 0;
    for (int i = 0; i < 16; i++)
        if (block[i] != 0) n++;
    return n;
}

int main(void) {
    blunt_init_audio_quant(80);
    int qp = ((100 - 80) * 63) / 100;

    int nsamp = 32;
    int16_t in[64];
    for (int i = 0; i < nsamp; i++) {
        double t = (double)i / 44100.0;
        double val = sin(2.0 * 3.14159265358979 * 440.0 * t) * 16000.0;
        in[i*2+0] = (int16_t)val;
        in[i*2+1] = (int16_t)(val * 0.7);
    }

    int16_t block[16];
    int32_t coded[16];
    int32_t zigzag[16];

    // encode each block and check nz count
    for (int ch = 0; ch < 2; ch++) {
        for (int b = 0; b < 2; b++) {
            memset(block, 0, sizeof(block));
            for (int i = 0; i < 16; i++) {
                int idx = b * 16 + i;
                if (idx < nsamp)
                    block[i] = in[idx * 2 + ch];
            }
            blunt_audio_encode_block(block, coded, qp);
            for (int i = 0; i < 16; i++)
                zigzag[i] = coded[blunt_zigzag_scan[i]];
            int nz = count_nz(zigzag);
            printf("ch%d b%d: %d non-zero coefficients out of 16\n", ch, b, nz);
        }
    }
    return 0;
}
