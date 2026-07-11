#include "blunt_audio.h"
#include "blunt_tables.h"
#include <stdio.h>
#include <string.h>

int main(void) {
    printf("=== Minimal Stereo Codec Test ===\n\n");
    blunt_init_audio_quant(80);
    int qp = ((100 - 80) * 63) / 100;

    /* Just 16 stereo samples = exactly 1 block per channel */
    int16_t in[32];
    for (int i = 0; i < 16; i++) {
        in[i*2+0] = (int16_t)(1000 + i * 100);
        in[i*2+1] = (int16_t)(2000 + i * 50);
    }

    printf("Input first 4 stereo pairs:\n");
    for (int i = 0; i < 4; i++)
        printf("  [%d] ch0=%6d ch1=%6d\n", i, in[i*2], in[i*2+1]);

    /* Encode: 1 block per channel */
    uint8_t bs[8192];
    int bsz = blunt_audio_encode_frame(in, 16, 2, qp, bs, sizeof(bs));
    printf("\nEncoded %d bytes for 16 stereo samples (1 block/ch)\n", bsz);

    /* Decode with out_cap = 32 (correct) */
    int16_t out1[32];
    memset(out1, 0, sizeof(out1));
    int n1 = blunt_audio_decode_frame(bs, bsz, 2, qp, out1, 32);
    printf("Decode out_cap=32: wrote %d values\n", n1);

    printf("\n  out_cap=32 results:\n");
    for (int i = 0; i < 16; i++) {
        int ok0 = (in[i*2+0] == out1[i*2+0]);
        int ok1 = (in[i*2+1] == out1[i*2+1]);
        if (!ok0 || !ok1)
            printf("  [%2d] ch0: %6d -> %6d %s  ch1: %6d -> %6d %s\n",
                   i, in[i*2+0], out1[i*2+0], ok0 ? "OK" : "FAIL",
                   in[i*2+1], out1[i*2+1], ok1 ? "OK" : "FAIL");
    }

    /* Decode with out_cap = 64 (too big) */
    int16_t out2[64];
    memset(out2, 0, sizeof(out2));
    int n2 = blunt_audio_decode_frame(bs, bsz, 2, qp, out2, 64);
    printf("\nDecode out_cap=64: wrote %d values\n", n2);

    printf("  out_cap=64 results (first 16 pairs):\n");
    for (int i = 0; i < 16; i++) {
        int ok0 = (in[i*2+0] == out2[i*2+0]);
        int ok1 = (in[i*2+1] == out2[i*2+1]);
        if (!ok0 || !ok1)
            printf("  [%2d] ch0: %6d -> %6d %s  ch1: %6d -> %6d %s\n",
                   i, in[i*2+0], out2[i*2+0], ok0 ? "OK" : "FAIL",
                   in[i*2+1], out2[i*2+1], ok1 ? "OK" : "FAIL");
    }

    /* Mono test with same data */
    printf("\n=== Mono test (ch0 data only) ===\n");
    int16_t mono[16];
    for (int i = 0; i < 16; i++) mono[i] = in[i*2+0];

    uint8_t mbs[8192];
    int mbsz = blunt_audio_encode_frame(mono, 16, 1, qp, mbs, sizeof(mbs));
    printf("Mono encoded %d bytes\n", mbsz);

    int16_t mout[16];
    memset(mout, 0, sizeof(mout));
    int mn = blunt_audio_decode_frame(mbs, mbsz, 1, qp, mout, 16);
    printf("Mono decoded %d values\n", mn);

    int mok = 1;
    for (int i = 0; i < 16; i++) {
        if (mono[i] != mout[i]) {
            printf("  FAIL [%d]: %d != %d\n", i, mono[i], mout[i]);
            mok = 0;
        }
    }
    if (mok) printf("  PASS: mono lossless\n");

    return 0;
}
