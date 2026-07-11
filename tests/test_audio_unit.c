#include "blunt_audio.h"
#include "blunt_tables.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

int main(void) {
    printf("=== Stereo Audio Debug ===\n\n");
    blunt_init_audio_quant(0);

    int16_t stereo[64];
    for (int i = 0; i < 32; i++) {
        stereo[i*2+0] = (int16_t)(1000 + i * 100);
        stereo[i*2+1] = (int16_t)(2000 + i * 50);
    }

    uint8_t bs[8192];
    int bsz = blunt_audio_encode_frame(stereo, 32, 2, 0, bs, sizeof(bs));
    printf("Encoded %d bytes for 32 stereo samples\n\n", bsz);

    int16_t sout[128];
    memset(sout, 0, sizeof(sout));
    int sn = blunt_audio_decode_frame(bs, bsz, 2, 0, sout, 128);
    printf("Decoded %d values\n\n", sn);

    printf("Expected vs Got:\n");
    for (int i = 0; i < 32; i++) {
        int exp0 = stereo[i*2+0], exp1 = stereo[i*2+1];
        int got0 = sout[i*2+0], got1 = sout[i*2+1];
        int ok0 = (exp0 == got0), ok1 = (exp1 == got1);
        if (!ok0 || !ok1) {
            printf("  [%2d] ch0: %6d -> %6d %s  ch1: %6d -> %6d %s\n",
                   i, exp0, got0, ok0 ? "OK" : "FAIL",
                   exp1, got1, ok1 ? "OK" : "FAIL");
        }
    }

    /* Now try mono 32 samples (same data, one channel) */
    printf("\n=== Mono test (same data, ch=1) ===\n");
    int16_t mono[32];
    for (int i = 0; i < 32; i++) mono[i] = 1000 + i * 100;

    uint8_t mbs[8192];
    int mbsz = blunt_audio_encode_frame(mono, 32, 1, 0, mbs, sizeof(mbs));
    printf("Encoded %d bytes\n", mbsz);

    int16_t mout[64];
    memset(mout, 0, sizeof(mout));
    int mn = blunt_audio_decode_frame(mbs, mbsz, 1, 0, mout, 64);
    printf("Decoded %d values\n", mn);

    int mok = 1;
    for (int i = 0; i < 32; i++) {
        if (mono[i] != mout[i]) {
            printf("  FAIL [%d]: %d != %d\n", i, mono[i], mout[i]);
            mok = 0;
        }
    }
    if (mok) printf("  PASS: mono lossless\n");

    /* Test: encode ch0 only, then ch1 only, compare with stereo */
    printf("\n=== Separate channel test ===\n");
    int16_t ch0[32], ch1[32];
    for (int i = 0; i < 32; i++) {
        ch0[i] = stereo[i*2+0];
        ch1[i] = stereo[i*2+1];
    }

    uint8_t c0bs[8192], c1bs[8192];
    int c0sz = blunt_audio_encode_frame(ch0, 32, 1, 0, c0bs, sizeof(c0bs));
    int c1sz = blunt_audio_encode_frame(ch1, 32, 1, 0, c1bs, sizeof(c1bs));
    printf("ch0: %d bytes, ch1: %d bytes, stereo: %d bytes\n", c0sz, c1sz, bsz);
    printf("Expected stereo size: %d bytes\n", c0sz + c1sz);

    /* Check if the stereo bitstream starts with ch0's bitstream */
    if (bsz >= c0sz && memcmp(bs, c0bs, c0sz) == 0)
        printf("Stereo starts with ch0 bitstream: MATCH\n");
    else
        printf("Stereo starts with ch0 bitstream: MISMATCH\n");

    return 0;
}
