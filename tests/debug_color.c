#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "blunt.h"

int main(void) {
    const int W = 320;
    const int H = 240;
    printf("=== Color Conversion Roundtrip Test ===\n");

    uint8_t *rgb_in = (uint8_t *)malloc(W * H * 3);
    uint8_t *rgb_out = (uint8_t *)malloc(W * H * 3);

    /* Generate gradient frame */
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            int off = (y * W + x) * 3;
            rgb_in[off + 0] = (uint8_t)((x * 255) / W);
            rgb_in[off + 1] = (uint8_t)((y * 255) / H);
            rgb_in[off + 2] = (uint8_t)(((x + y) * 255) / (W + H));
        }

    /* RGB -> YCbCr -> RGB roundtrip (no encode/decode) */
    BluntFrame ycbcr;
    memset(&ycbcr, 0, sizeof(ycbcr));
    blunt_rgb_to_ycbcr(rgb_in, W, H, W * 3, &ycbcr);
    blunt_ycbcr_to_rgb(&ycbcr, rgb_out, W, H, W * 3);

    double mse = 0;
    int max_err = 0;
    for (int j = 0; j < W * H * 3; j++) {
        double diff = (double)rgb_in[j] - (double)rgb_out[j];
        mse += diff * diff;
        int err = abs((int)rgb_in[j] - (int)rgb_out[j]);
        if (err > max_err) max_err = err;
    }
    mse /= (W * H * 3);
    printf("Color-only roundtrip PSNR: %.2f dB (max_err: %d)\n",
           mse > 0 ? 10.0*log10(255.0*255.0/mse) : 99.99, max_err);

    /* Now test full pipeline: RGB->YCbCr->encode->decode->YCbCr->RGB */
    BluntFrame ycbcr_in, ycbcr_out;
    memset(&ycbcr_in, 0, sizeof(ycbcr_in));
    memset(&ycbcr_out, 0, sizeof(ycbcr_out));

    BluntEncoder *enc = blunt_encoder_create();
    blunt_encoder_set_quality(enc, 80);
    blunt_encoder_open(enc, "debug_color.blunt", W, H, 24, 1);
    blunt_rgb_to_ycbcr(rgb_in, W, H, W * 3, &ycbcr_in);
    blunt_encoder_write_frame(enc, &ycbcr_in, 1);
    blunt_encoder_close(enc);
    blunt_encoder_destroy(enc);

    BluntDecoder *dec = blunt_decoder_create();
    blunt_decoder_open(dec, "debug_color.blunt");
    blunt_decoder_read_frame(dec, &ycbcr_out);
    blunt_ycbcr_to_rgb(&ycbcr_out, rgb_out, W, H, W * 3);

    mse = 0; max_err = 0;
    for (int j = 0; j < W * H * 3; j++) {
        double diff = (double)rgb_in[j] - (double)rgb_out[j];
        mse += diff * diff;
        int err = abs((int)rgb_in[j] - (int)rgb_out[j]);
        if (err > max_err) max_err = err;
    }
    mse /= (W * H * 3);
    printf("Full pipeline PSNR: %.2f dB (max_err: %d)\n",
           mse > 0 ? 10.0*log10(255.0*255.0/mse) : 99.99, max_err);

    /* Show sample pixels */
    printf("\nSample RGB pixels:\n");
    for (int x = 0; x < 8; x++) {
        int off = x * 3;
        printf("  [%d] in=(%d,%d,%d) out=(%d,%d,%d)\n", x,
               rgb_in[off], rgb_in[off+1], rgb_in[off+2],
               rgb_out[off], rgb_out[off+1], rgb_out[off+2]);
    }

    blunt_decoder_destroy(dec);
    blunt_frame_free(&ycbcr_in);
    blunt_frame_free(&ycbcr_out);
    free(rgb_in);
    free(rgb_out);
    return 0;
}
