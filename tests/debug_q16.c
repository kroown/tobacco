#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "blunt.h"

int main(void) {
    /* 16x16 gradient macroblock */
    const int W = 16, H = 16;
    uint8_t rgb[W * H * 3];
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            int off = (y * W + x) * 3;
            rgb[off + 0] = (uint8_t)((x * 255) / W);
            rgb[off + 1] = (uint8_t)((y * 255) / H);
            rgb[off + 2] = (uint8_t)(((x + y) * 255) / (W + H));
        }

    for (int quality = 80; quality <= 100; quality += 10) {
        BluntFrame y_in, y_out;
        memset(&y_in, 0, sizeof(y_in));
        memset(&y_out, 0, sizeof(y_out));
        blunt_rgb_to_ycbcr(rgb, W, H, W * 3, &y_in);

        char fname[64];
        sprintf(fname, "debug_q%d_16x16.blunt", quality);

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
        int errs = 0;
        for (int y = 0; y < H; y++) {
            for (int x = 0; x < W; x++) {
                int orig = y_in.y[y * y_in.y_stride + x];
                int decv = y_out.y[y * y_out.y_stride + x];
                double diff = (double)orig - (double)decv;
                mse += diff * diff;
                if (orig != decv && errs < 3) {
                    printf("  q%d Y[%d][%d]: %d->%d\n", quality, y, x, orig, decv);
                    errs++;
                }
            }
        }
        mse /= (W * H);
        printf("Quality %3d: Y PSNR = %.2f dB\n\n", quality,
               mse > 0 ? 10.0 * log10(255.0 * 255.0 / mse) : 99.99);

        blunt_frame_free(&y_in);
        blunt_frame_free(&y_out);
    }
    return 0;
}
