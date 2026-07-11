#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "blunt.h"

int main(void) {
    printf("=== Single Macroblock I-Frame Test ===\n");

    /* 16x16 = one macroblock, all Y=223, Cb/Cr=128 */
    BluntFrame ycbcr_in, ycbcr_out;
    memset(&ycbcr_in, 0, sizeof(ycbcr_in));
    memset(&ycbcr_out, 0, sizeof(ycbcr_out));
    blunt_frame_alloc(&ycbcr_in, 1, 1);
    blunt_frame_alloc(&ycbcr_out, 1, 1);

    /* Fill Y with 223 */
    for (int y = 0; y < 16; y++)
        for (int x = 0; x < 16; x++)
            ycbcr_in.y[y * ycbcr_in.y_stride + x] = 223;
    /* Fill Cb/Cr with 128 */
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 8; x++) {
            ycbcr_in.cb[y * ycbcr_in.cb_stride + x] = 128;
            ycbcr_in.cr[y * ycbcr_in.cr_stride + x] = 128;
        }

    BluntEncoder *enc = blunt_encoder_create();
    blunt_encoder_set_quality(enc, 80);
    blunt_encoder_open(enc, "debug_1mb.blunt", 16, 16, 24, 1);
    blunt_encoder_write_frame(enc, &ycbcr_in, 1);
    blunt_encoder_close(enc);
    blunt_encoder_destroy(enc);

    BluntDecoder *dec = blunt_decoder_create();
    blunt_decoder_open(dec, "debug_1mb.blunt");
    blunt_decoder_read_frame(dec, &ycbcr_out);

    double mse = 0;
    for (int y = 0; y < 16; y++)
        for (int x = 0; x < 16; x++) {
            int orig = ycbcr_in.y[y * ycbcr_in.y_stride + x];
            int decv = ycbcr_out.y[y * ycbcr_out.y_stride + x];
            if (abs(orig - decv) > 0) {
                printf("  Y[%d][%d]: orig=%d dec=%d err=%d\n", y, x, orig, decv, abs(orig-decv));
            }
            mse += (double)(orig - decv) * (orig - decv);
        }
    mse /= 256;
    printf("Y PSNR: %.2f dB\n", mse > 0 ? 10.0*log10(255.0*255.0/mse) : 99.99);

    blunt_decoder_destroy(dec);
    blunt_frame_free(&ycbcr_in);
    blunt_frame_free(&ycbcr_out);

    /* Now test with 32x16 = 2 macroblocks, left=223, right=29 */
    printf("\n=== Two Macroblock Test ===\n");
    blunt_frame_alloc(&ycbcr_in, 2, 1);
    blunt_frame_alloc(&ycbcr_out, 2, 1);

    for (int y = 0; y < 16; y++)
        for (int x = 0; x < 32; x++)
            ycbcr_in.y[y * ycbcr_in.y_stride + x] = (x < 16) ? 223 : 29;
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 16; x++) {
            ycbcr_in.cb[y * ycbcr_in.cb_stride + x] = 128;
            ycbcr_in.cr[y * ycbcr_in.cr_stride + x] = 128;
        }

    enc = blunt_encoder_create();
    blunt_encoder_set_quality(enc, 80);
    blunt_encoder_open(enc, "debug_2mb.blunt", 32, 16, 24, 1);
    blunt_encoder_write_frame(enc, &ycbcr_in, 1);
    blunt_encoder_close(enc);
    blunt_encoder_destroy(enc);

    dec = blunt_decoder_create();
    blunt_decoder_open(dec, "debug_2mb.blunt");
    blunt_decoder_read_frame(dec, &ycbcr_out);

    printf("MB0 (should be 223):\n");
    for (int x = 0; x < 16; x++)
        printf("  Y[0][%d]: orig=%d dec=%d\n", x,
               ycbcr_in.y[x], ycbcr_out.y[x]);
    printf("MB1 (should be 29):\n");
    for (int x = 16; x < 32; x++)
        printf("  Y[0][%d]: orig=%d dec=%d\n", x,
               ycbcr_in.y[x], ycbcr_out.y[x]);

    mse = 0;
    for (int y = 0; y < 16; y++)
        for (int x = 0; x < 32; x++) {
            int orig = ycbcr_in.y[y * ycbcr_in.y_stride + x];
            int decv = ycbcr_out.y[y * ycbcr_out.y_stride + x];
            mse += (double)(orig - decv) * (orig - decv);
        }
    mse /= (16 * 32);
    printf("Y PSNR: %.2f dB\n", mse > 0 ? 10.0*log10(255.0*255.0/mse) : 99.99);

    blunt_decoder_destroy(dec);
    blunt_frame_free(&ycbcr_in);
    blunt_frame_free(&ycbcr_out);
    return 0;
}
