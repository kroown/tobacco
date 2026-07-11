#include "blunt_audio.h"
#include "blunt_tables.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

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

    // encode ch1 b0 block manually to get exact zigzag values
    int16_t block[16];
    int32_t coded[16], zigzag[16];

    for (int ch = 0; ch < 2; ch++) {
        for (int b = 0; b < 2; b++) {
            memset(block, 0, sizeof(block));
            for (int i = 0; i < 16; i++) {
                int idx = b * 16 + i;
                if (idx < nsamp) block[i] = in[idx * 2 + ch];
            }
            blunt_audio_encode_block(block, coded, qp);
            for (int i = 0; i < 16; i++)
                zigzag[i] = coded[blunt_zigzag_scan[i]];
            printf("ch%d b%d zigzag: ", ch, b);
            for (int i = 0; i < 16; i++) printf("%d ", zigzag[i]);
            printf("\n");
        }
    }

    // now use the actual library to encode the full frame
    uint8_t bs[65536];
    int bsz = blunt_audio_encode_frame(in, nsamp, 2, qp, bs, sizeof(bs));
    printf("\nbitstream size: %d bytes\n", bsz);

    // print hex around byte 83 (where ch1 b0 should start)
    printf("\nbytes 80-90:\n");
    for (int i = 80; i < 90 && i < bsz; i++)
        printf("  [%3d] 0x%02X\n", i, bs[i]);

    // Now decode using the library and check
    int16_t out[64] = {0};
    int n = blunt_audio_decode_frame(bs, bsz, 2, qp, out, nsamp * 2);
    printf("\nlibrary decode returned %d samples\n", n);
    for (int i = 0; i < 16; i++)
        printf("  ch1[%2d] expected=%6d got=%6d diff=%d\n", i, in[i*2+1], out[i*2+1], in[i*2+1]-out[i*2+1]);

    return 0;
}
