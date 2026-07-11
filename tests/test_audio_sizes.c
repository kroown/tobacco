#include "blunt_audio.h"
#include "blunt_tables.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

int main(void) {
    blunt_init_audio_quant(80);
    int qp = ((100 - 80) * 63) / 100;

    /* Test different sizes to find where stereo breaks */
    int sizes[] = {16, 32, 48, 64, 128, 256, 1024, 1470};
    int nsizes = sizeof(sizes) / sizeof(sizes[0]);

    for (int s = 0; s < nsizes; s++) {
        int nsamp = sizes[s];
        int16_t *in = (int16_t *)calloc(nsamp * 2, sizeof(int16_t));
        for (int i = 0; i < nsamp; i++) {
            in[i*2+0] = (int16_t)(1000 + i);
            in[i*2+1] = (int16_t)(2000 + i);
        }

        uint8_t bs[65536];
        int bsz = blunt_audio_encode_frame(in, nsamp, 2, qp, bs, sizeof(bs));

        int16_t *out = (int16_t *)calloc(nsamp * 2, sizeof(int16_t));
        int n = blunt_audio_decode_frame(bs, bsz, 2, qp, out, nsamp * 2);

        int ch0_ok = 0, ch1_ok = 0;
        for (int i = 0; i < nsamp; i++) {
            if (in[i*2+0] == out[i*2+0]) ch0_ok++;
            if (in[i*2+1] == out[i*2+1]) ch1_ok++;
        }

        printf("nsamp=%4d bsz=%5d n=%5d ch0_match=%d/%d ch1_match=%d/%d\n",
               nsamp, bsz, n, ch0_ok, nsamp, ch1_ok, nsamp);

        free(in);
        free(out);
    }

    return 0;
}
