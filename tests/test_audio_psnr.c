#include "blunt_audio.h"
#include "blunt_tables.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

int main(void) {
    blunt_init_audio_quant(80);
    int qp = ((100 - 80) * 63) / 100;

    int sizes[] = {16, 32, 64, 128, 256, 512, 1024, 1470};
    int nsizes = sizeof(sizes) / sizeof(sizes[0]);

    for (int s = 0; s < nsizes; s++) {
        int nsamp = sizes[s];
        int16_t *in = (int16_t *)calloc(nsamp * 2, sizeof(int16_t));
        for (int i = 0; i < nsamp; i++) {
            double t = (double)i / 44100.0;
            double val = sin(2.0 * 3.14159265358979 * 440.0 * t) * 16000.0;
            in[i*2+0] = (int16_t)val;
            in[i*2+1] = (int16_t)(val * 0.7);
        }

        uint8_t bs[65536];
        int bsz = blunt_audio_encode_frame(in, nsamp, 2, qp, bs, sizeof(bs));

        int16_t *out = (int16_t *)calloc(nsamp * 2, sizeof(int16_t));
        int n = blunt_audio_decode_frame(bs, bsz, 2, qp, out, nsamp * 2);

        double total_err = 0.0;
        double ch0_err = 0.0, ch1_err = 0.0;
        for (int i = 0; i < nsamp; i++) {
            double d0 = (double)in[i*2+0] - (double)out[i*2+0];
            double d1 = (double)in[i*2+1] - (double)out[i*2+1];
            ch0_err += d0 * d0;
            ch1_err += d1 * d1;
            total_err += d0 * d0 + d1 * d1;
        }
        double mse = total_err / (nsamp * 2);
        double psnr = (mse > 0) ? 10.0 * log10(32767.0 * 32767.0 / mse) : 999.0;

        double mse0 = ch0_err / nsamp;
        double mse1 = ch1_err / nsamp;
        double psnr0 = (mse0 > 0) ? 10.0 * log10(32767.0 * 32767.0 / mse0) : 999.0;
        double psnr1 = (mse1 > 0) ? 10.0 * log10(32767.0 * 32767.0 / mse1) : 999.0;

        printf("nsamp=%4d bsz=%5d PSNR=%.1f dB (ch0=%.1f ch1=%.1f)\n",
               nsamp, bsz, psnr, psnr0, psnr1);

        free(in);
        free(out);
    }

    return 0;
}
