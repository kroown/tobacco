#include "blunt_audio.h"
#include "blunt_tables.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

int main(void) {
    blunt_init_audio_quant(80);
    int qp = ((100 - 80) * 63) / 100;

    /* Test 1: Single block through full pipeline (mono) */
    printf("=== Test 1: Single block mono through encode_frame/decode_frame ===\n");
    int16_t mono16[16];
    for (int i = 0; i < 16; i++) mono16[i] = (int16_t)(1000 + i * 100);

    uint8_t bs[8192];
    int bsz = blunt_audio_encode_frame(mono16, 16, 1, qp, bs, sizeof(bs));
    printf("  Encoded %d bytes\n", bsz);

    int16_t out16[16] = {0};
    int n = blunt_audio_decode_frame(bs, bsz, 1, qp, out16, 16);
    printf("  Decoded %d values\n", n);

    double err = 0;
    for (int i = 0; i < 16; i++) {
        double d = (double)mono16[i] - (double)out16[i];
        err += d * d;
        printf("  [%2d] %6d -> %6d diff=%d\n", i, mono16[i], out16[i], mono16[i] - out16[i]);
    }
    double mse = err / 16;
    double psnr = (mse > 0) ? 10.0 * log10(32767.0 * 32767.0 / mse) : 999.0;
    printf("  Mono PSNR: %.1f dB\n\n", psnr);

    /* Test 2: Single block stereo through full pipeline */
    printf("=== Test 2: Single block stereo ===\n");
    int16_t stereo32[32];
    for (int i = 0; i < 16; i++) {
        stereo32[i*2+0] = (int16_t)(1000 + i * 100);
        stereo32[i*2+1] = (int16_t)(2000 + i * 50);
    }

    bsz = blunt_audio_encode_frame(stereo32, 16, 2, qp, bs, sizeof(bs));
    printf("  Encoded %d bytes\n", bsz);

    int16_t out32[32] = {0};
    n = blunt_audio_decode_frame(bs, bsz, 2, qp, out32, 32);
    printf("  Decoded %d values\n", n);

    double err0 = 0, err1 = 0;
    for (int i = 0; i < 16; i++) {
        double d0 = (double)stereo32[i*2+0] - (double)out32[i*2+0];
        double d1 = (double)stereo32[i*2+1] - (double)out32[i*2+1];
        err0 += d0 * d0;
        err1 += d1 * d1;
        printf("  [%2d] ch0: %6d -> %6d %s  ch1: %6d -> %6d %s\n",
               i, stereo32[i*2+0], out32[i*2+0],
               stereo32[i*2+0] == out32[i*2+0] ? "OK" : "diff",
               stereo32[i*2+1], out32[i*2+1],
               stereo32[i*2+1] == out32[i*2+1] ? "OK" : "diff");
    }
    double mse0 = err0 / 16, mse1 = err1 / 16;
    double psnr0 = (mse0 > 0) ? 10.0 * log10(32767.0 * 32767.0 / mse0) : 999.0;
    double psnr1 = (mse1 > 0) ? 10.0 * log10(32767.0 * 32767.0 / mse1) : 999.0;
    printf("  ch0 PSNR: %.1f dB, ch1 PSNR: %.1f dB\n\n", psnr0, psnr1);

    /* Test 3: Direct block encode/decode (no runlevel) */
    printf("=== Test 3: Direct block encode/decode (no runlevel) ===\n");
    int32_t coded[16];
    int16_t block_out[16];
    blunt_audio_encode_block(stereo32, coded, qp);
    blunt_audio_decode_block(coded, block_out, qp);

    double derr = 0;
    for (int i = 0; i < 16; i++) {
        double d = (double)stereo32[i] - (double)block_out[i];
        derr += d * d;
    }
    double dmse = derr / 16;
    double dpsnr = (dmse > 0) ? 10.0 * log10(32767.0 * 32767.0 / dmse) : 999.0;
    printf("  Direct block PSNR: %.1f dB\n\n", dpsnr);

    /* Test 4: Two blocks mono */
    printf("=== Test 4: Two blocks mono (32 samples) ===\n");
    int16_t mono32[32];
    for (int i = 0; i < 32; i++) mono32[i] = (int16_t)(1000 + i * 50);

    bsz = blunt_audio_encode_frame(mono32, 32, 1, qp, bs, sizeof(bs));
    printf("  Encoded %d bytes\n", bsz);

    int16_t mout32[32] = {0};
    n = blunt_audio_decode_frame(bs, bsz, 1, qp, mout32, 32);
    printf("  Decoded %d values\n", n);

    err = 0;
    for (int i = 0; i < 32; i++) {
        double d = (double)mono32[i] - (double)mout32[i];
        err += d * d;
    }
    mse = err / 32;
    psnr = (mse > 0) ? 10.0 * log10(32767.0 * 32767.0 / mse) : 999.0;
    printf("  Mono 2-block PSNR: %.1f dB\n", psnr);
    for (int i = 0; i < 32; i++) {
        printf("  [%2d] %6d -> %6d diff=%d\n", i, mono32[i], mout32[i], mono32[i] - mout32[i]);
    }

    return 0;
}
