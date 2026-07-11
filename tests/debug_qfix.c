#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "blunt.h"

int main(void) {
    const int W = 32, H = 32;

    for (int quality = 80; quality <= 100; quality += 5) {
        uint8_t *rgb = (uint8_t *)calloc(W * H * 3, 1);
        for (int y = 0; y < H; y++)
            for (int x = 0; x < W; x++) {
                int off = (y * W + x) * 3;
                rgb[off + 0] = (uint8_t)((x * 255) / W);
                rgb[off + 1] = (uint8_t)((y * 255) / H);
                rgb[off + 2] = (uint8_t)(((x + y) * 255) / (W + H));
            }

        BluntFrame y_in, y_out;
        memset(&y_in, 0, sizeof(y_in));
        memset(&y_out, 0, sizeof(y_out));
        blunt_rgb_to_ycbcr(rgb, W, H, W * 3, &y_in);

        char fname[64];
        sprintf(fname, "debug_q%d_32x32.blunt", quality);

        BluntEncoder *enc = blunt_encoder_create();
        blunt_encoder_set_quality(enc, quality);
        blunt_encoder_open(enc, fname, W, H, 24, 1);
        blunt_encoder_write_frame(enc, &y_in, 1);
        blunt_encoder_close(enc);
        blunt_encoder_destroy(enc);

        BluntDecoder *dec = blunt_decoder_create();
        blunt_decoder_open(dec, fname);
        blunt_decoder_read_frame(dec, &y_out);
        blunt_decoder_destroy(dec);

        double mse = 0;
        int y_count = y_out.y_stride * H;
        for (int j = 0; j < y_count; j++) {
            double diff = (double)y_in.y[j] - (double)y_out.y[j];
            mse += diff * diff;
        }
        int c_count = y_out.cb_stride * ((H + 1) / 2);
        for (int j = 0; j < c_count; j++) {
            double diff = (double)y_in.cb[j] - (double)y_out.cb[j];
            mse += diff * diff;
            diff = (double)y_in.cr[j] - (double)y_out.cr[j];
            mse += diff * diff;
        }
        mse /= (y_count + c_count * 2);
        double psnr = (mse > 0) ? 10.0 * log10(255.0 * 255.0 / mse) : 99.99;

        printf("Quality %3d: YCbCr PSNR = %.2f dB", quality, psnr);

        /* Show first few Y errors */
        int errs = 0;
        for (int j = 0; j < y_count && errs < 5; j++) {
            if (y_in.y[j] != y_out.y[j]) {
                printf("  Y[%d]: %d->%d", j, y_in.y[j], y_out.y[j]);
                errs++;
            }
        }
        printf("\n");

        blunt_frame_free(&y_in);
        blunt_frame_free(&y_out);
        free(rgb);
    }
    return 0;
}
