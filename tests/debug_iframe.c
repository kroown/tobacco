#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "blunt.h"

int main(void) {
    const int W = 16;
    const int H = 16;
    printf("=== I-Frame Debug Test ===\n");

    /* Create a simple YCbCr frame: Y=128, Cb=128, Cr=128 everywhere */
    BluntFrame ycbcr_in, ycbcr_out;
    memset(&ycbcr_in, 0, sizeof(ycbcr_in));
    memset(&ycbcr_out, 0, sizeof(ycbcr_out));

    int mbw = 1, mbh = 1;
    blunt_frame_alloc(&ycbcr_in, mbw, mbh);
    blunt_frame_alloc(&ycbcr_out, mbw, mbh);

    /* Fill Y plane with 128 */
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++)
            ycbcr_in.y[y * ycbcr_in.y_stride + x] = 128;
    /* Fill Cb/Cr with 128 */
    for (int y = 0; y < H/2; y++)
        for (int x = 0; x < W/2; x++) {
            ycbcr_in.cb[y * ycbcr_in.cb_stride + x] = 128;
            ycbcr_in.cr[y * ycbcr_in.cr_stride + x] = 128;
        }

    printf("Input Y[0][0] = %d\n", ycbcr_in.y[0]);

    /* Encode */
    BluntEncoder *enc = blunt_encoder_create();
    blunt_encoder_set_quality(enc, 80);
    if (blunt_encoder_open(enc, "debug_iframe.blunt", W, H, 24, 1) != 0) {
        fprintf(stderr, "FAIL: encoder open\n");
        return 1;
    }
    blunt_encoder_write_frame(enc, &ycbcr_in, 1);
    blunt_encoder_close(enc);
    blunt_encoder_destroy(enc);
    printf("Encoded.\n");

    /* Decode */
    BluntDecoder *dec = blunt_decoder_create();
    if (blunt_decoder_open(dec, "debug_iframe.blunt") != 0) {
        fprintf(stderr, "FAIL: decoder open\n");
        return 1;
    }
    BluntHeader hdr;
    blunt_decoder_read_header(dec, &hdr);
    printf("Header: %dx%d, %u frames, quality=%d\n",
           hdr.width, hdr.height, hdr.num_frames, hdr.quality);

    if (blunt_decoder_read_frame(dec, &ycbcr_out) != 0) {
        fprintf(stderr, "FAIL: decode frame\n");
        return 1;
    }
    printf("Decoded.\n");

    printf("Output Y[0][0] = %d (expected 128)\n", ycbcr_out.y[0]);

    /* Check all Y values */
    int errors = 0;
    int max_err = 0;
    double mse = 0;
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            int orig = ycbcr_in.y[y * ycbcr_in.y_stride + x];
            int dec  = ycbcr_out.y[y * ycbcr_out.y_stride + x];
            int err = abs(orig - dec);
            if (err > max_err) max_err = err;
            mse += (double)(orig - dec) * (orig - dec);
            if (err > 0 && errors < 5) {
                printf("  Y[%d][%d]: orig=%d dec=%d err=%d\n", y, x, orig, dec, err);
                errors++;
            }
        }
    }
    mse /= (W * H);
    printf("Y plane MSE: %.2f, max_err: %d\n", mse, max_err);
    printf("Y PSNR: %.2f dB\n", mse > 0 ? 10.0*log10(255.0*255.0/mse) : 99.99);

    /* Now test with gradient */
    printf("\n=== Gradient Test ===\n");
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++)
            ycbcr_in.y[y * ycbcr_in.y_stride + x] = (uint8_t)((x * 255) / W);
    for (int y = 0; y < H/2; y++)
        for (int x = 0; x < W/2; x++) {
            ycbcr_in.cb[y * ycbcr_in.cb_stride + x] = 128;
            ycbcr_in.cr[y * ycbcr_in.cr_stride + x] = 128;
        }

    enc = blunt_encoder_create();
    blunt_encoder_set_quality(enc, 80);
    blunt_encoder_open(enc, "debug_gradient.blunt", W, H, 24, 1);
    blunt_encoder_write_frame(enc, &ycbcr_in, 1);
    blunt_encoder_close(enc);
    blunt_encoder_destroy(enc);

    dec = blunt_decoder_create();
    blunt_decoder_open(dec, "debug_gradient.blunt");
    blunt_decoder_read_header(dec, &hdr);
    blunt_decoder_read_frame(dec, &ycbcr_out);

    mse = 0;
    max_err = 0;
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            int orig = ycbcr_in.y[y * ycbcr_in.y_stride + x];
            int dec  = ycbcr_out.y[y * ycbcr_out.y_stride + x];
            int err = abs(orig - dec);
            if (err > max_err) max_err = err;
            mse += (double)(orig - dec) * (orig - dec);
            printf("  Y[%d][%d]: orig=%d dec=%d err=%d\n", y, x, orig, dec, err);
        }
    }
    mse /= (W * H);
    printf("Y MSE: %.2f, max_err: %d, PSNR: %.2f dB\n",
           mse, max_err, mse > 0 ? 10.0*log10(255.0*255.0/mse) : 99.99);

    blunt_decoder_destroy(dec);
    blunt_frame_free(&ycbcr_in);
    blunt_frame_free(&ycbcr_out);
    return 0;
}
