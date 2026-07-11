#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "blunt.h"

int main(void) {
    const int W = 320;
    const int H = 240;
    printf("=== P-Frame Identity Test ===\n");

    uint8_t *rgb = (uint8_t *)malloc(W * H * 3);
    BluntFrame ycbcr_in, ycbcr_out;
    memset(&ycbcr_in, 0, sizeof(ycbcr_in));
    memset(&ycbcr_out, 0, sizeof(ycbcr_out));

    /* Generate same pattern for all frames */
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            int off = (y * W + x) * 3;
            rgb[off + 0] = (uint8_t)((x * 255) / W);
            rgb[off + 1] = (uint8_t)((y * 255) / H);
            rgb[off + 2] = (uint8_t)(((x + y) * 255) / (W + H));
        }
    blunt_rgb_to_ycbcr(rgb, W, H, W * 3, &ycbcr_in);

    BluntEncoder *enc = blunt_encoder_create();
    blunt_encoder_set_quality(enc, 80);
    blunt_encoder_open(enc, "test_identity.blunt", W, H, 24, 1);
    /* Frame 0: I-frame */
    blunt_encoder_write_frame(enc, &ycbcr_in, 1);
    /* Frame 1-4: P-frames with SAME content */
    for (int i = 1; i < 4; i++)
        blunt_encoder_write_frame(enc, &ycbcr_in, 0);
    blunt_encoder_close(enc);
    blunt_encoder_destroy(enc);

    BluntDecoder *dec = blunt_decoder_create();
    blunt_decoder_open(dec, "test_identity.blunt");
    for (int i = 0; i < 4; i++) {
        blunt_decoder_read_frame(dec, &ycbcr_out);

        double mse = 0;
        int y_count = ycbcr_out.y_stride * H;
        int c_count = ycbcr_out.cb_stride * (H/2);
        for (int j = 0; j < y_count; j++)
            mse += (double)((int)ycbcr_in.y[j] - (int)ycbcr_out.y[j]) * ((int)ycbcr_in.y[j] - (int)ycbcr_out.y[j]);
        for (int j = 0; j < c_count; j++) {
            mse += (double)((int)ycbcr_in.cb[j] - (int)ycbcr_out.cb[j]) * ((int)ycbcr_in.cb[j] - (int)ycbcr_out.cb[j]);
            mse += (double)((int)ycbcr_in.cr[j] - (int)ycbcr_out.cr[j]) * ((int)ycbcr_in.cr[j] - (int)ycbcr_out.cr[j]);
        }
        int total = y_count + c_count * 2;
        mse /= total;
        printf("Frame %d (%c): YCbCr PSNR = %.2f dB\n", i,
               i == 0 ? 'I' : 'P',
               mse > 0 ? 10.0*log10(255.0*255.0/mse) : 99.99);
    }
    blunt_decoder_destroy(dec);

    /* Print some sample Y values */
    printf("\nSample Y values (row 0, first 32 pixels):\n");
    printf("  Orig: ");
    for (int x = 0; x < 32; x++) printf("%3d ", ycbcr_in.y[x]);
    printf("\n  Dec:  ");
    for (int x = 0; x < 32; x++) printf("%3d ", ycbcr_out.y[x]);
    printf("\n");

    free(rgb);
    blunt_frame_free(&ycbcr_in);
    blunt_frame_free(&ycbcr_out);
    return 0;
}
