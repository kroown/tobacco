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

    uint8_t bs[65536];
    int bsz = blunt_audio_encode_frame(in, nsamp, 2, qp, bs, sizeof(bs));
    printf("bitstream size: %d bytes\n", bsz);

    int16_t out[64] = {0};
    int n = blunt_audio_decode_frame(bs, bsz, 2, qp, out, nsamp * 2);
    printf("decoded %d samples\n\n", n);

    printf("ch0 block 0 (samples 0-15):\n");
    for (int i = 0; i < 16; i++)
        printf("  [%2d] %6d -> %6d diff=%d\n", i, in[i*2], out[i*2], in[i*2]-out[i*2]);

    printf("\nch0 block 1 (samples 16-31):\n");
    for (int i = 0; i < 16; i++)
        printf("  [%2d] %6d -> %6d diff=%d\n", i, in[(16+i)*2], out[(16+i)*2], in[(16+i)*2]-out[(16+i)*2]);

    printf("\nch1 block 0 (samples 0-15):\n");
    for (int i = 0; i < 16; i++)
        printf("  [%2d] %6d -> %6d diff=%d\n", i, in[i*2+1], out[i*2+1], in[i*2+1]-out[i*2+1]);

    printf("\nch1 block 1 (samples 16-31):\n");
    for (int i = 0; i < 16; i++)
        printf("  [%2d] %6d -> %6d diff=%d\n", i, in[(16+i)*2+1], out[(16+i)*2+1], in[(16+i)*2+1]-out[(16+i)*2+1]);

    double ch0_err = 0, ch1_err = 0;
    for (int i = 0; i < nsamp; i++) {
        double d0 = (double)in[i*2+0] - (double)out[i*2+0];
        double d1 = (double)in[i*2+1] - (double)out[i*2+1];
        ch0_err += d0*d0;
        ch1_err += d1*d1;
    }
    double mse0 = ch0_err / nsamp;
    double mse1 = ch1_err / nsamp;
    printf("\nch0 PSNR: %.1f dB, ch1 PSNR: %.1f dB\n",
           mse0 > 0 ? 10.0*log10(32767.0*32767.0/mse0) : 999.0,
           mse1 > 0 ? 10.0*log10(32767.0*32767.0/mse1) : 999.0);
    return 0;
}
