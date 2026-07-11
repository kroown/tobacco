#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "blunt.h"

int main(void) {
    const int W = 320;
    const int H = 240;
    printf("=== Large Frame Debug Test ===\n");
    printf("Resolution: %dx%d\n\n", W, H);

    BluntFrame ycbcr_in, ycbcr_out;
    memset(&ycbcr_in, 0, sizeof(ycbcr_in));
    memset(&ycbcr_out, 0, sizeof(ycbcr_out));

    int mbw = (W + 15) / 16;
    int mbh = (H + 15) / 16;
    blunt_frame_alloc(&ycbcr_in, mbw, mbh);
    blunt_frame_alloc(&ycbcr_out, mbw, mbh);

    printf("mbw=%d mbh=%d y_stride=%d cb_stride=%d\n",
           mbw, mbh, ycbcr_in.y_stride, ycbcr_in.cb_stride);

    /* Fill Y with gradient */
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++)
            ycbcr_in.y[y * ycbcr_in.y_stride + x] = (uint8_t)((x * 255) / W);
    /* Fill Cb/Cr with 128 */
    for (int y = 0; y < H/2; y++)
        for (int x = 0; x < W/2; x++) {
            ycbcr_in.cb[y * ycbcr_in.cb_stride + x] = 128;
            ycbcr_in.cr[y * ycbcr_in.cr_stride + x] = 128;
        }

    /* Encode I-frame only */
    BluntEncoder *enc = blunt_encoder_create();
    blunt_encoder_set_quality(enc, 80);
    if (blunt_encoder_open(enc, "debug_large.blunt", W, H, 24, 1) != 0) {
        fprintf(stderr, "FAIL: encoder open\n");
        return 1;
    }
    blunt_encoder_write_frame(enc, &ycbcr_in, 1);
    blunt_encoder_close(enc);
    blunt_encoder_destroy(enc);
    printf("Encoded.\n");

    /* Decode */
    BluntDecoder *dec = blunt_decoder_create();
    if (blunt_decoder_open(dec, "debug_large.blunt") != 0) {
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

    /* Compare Y plane */
    double mse = 0;
    int max_err = 0;
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            int orig = ycbcr_in.y[y * ycbcr_in.y_stride + x];
            int decv = ycbcr_out.y[y * ycbcr_out.y_stride + x];
            int err = abs(orig - decv);
            if (err > max_err) max_err = err;
            mse += (double)(orig - decv) * (orig - decv);
        }
    }
    mse /= (W * H);
    printf("Y MSE: %.2f, max_err: %d, PSNR: %.2f dB\n",
           mse, max_err, mse > 0 ? 10.0*log10(255.0*255.0/mse) : 99.99);

    /* Compare Cb plane */
    mse = 0; max_err = 0;
    int cw = W / 2, ch = H / 2;
    for (int y = 0; y < ch; y++)
        for (int x = 0; x < cw; x++) {
            int orig = ycbcr_in.cb[y * ycbcr_in.cb_stride + x];
            int decv = ycbcr_out.cb[y * ycbcr_out.cb_stride + x];
            int err = abs(orig - decv);
            if (err > max_err) max_err = err;
            mse += (double)(orig - decv) * (orig - decv);
        }
    mse /= (cw * ch);
    printf("Cb MSE: %.2f, max_err: %d, PSNR: %.2f dB\n",
           mse, max_err, mse > 0 ? 10.0*log10(255.0*255.0/mse) : 99.99);

    /* Print some sample pixels */
    printf("\nSample Y pixels (first macroblock, first row):\n");
    for (int x = 0; x < 16; x++) {
        int orig = ycbcr_in.y[x];
        int decv = ycbcr_out.y[x];
        printf("  Y[0][%d]: orig=%d dec=%d err=%d\n", x, orig, decv, abs(orig-decv));
    }
    printf("\nSample Y pixels (macroblock at x=16, row 0):\n");
    for (int x = 0; x < 16; x++) {
        int orig = ycbcr_in.y[16 + x];
        int decv = ycbcr_out.y[16 + x];
        printf("  Y[0][%d]: orig=%d dec=%d err=%d\n", 16+x, orig, decv, abs(orig-decv));
    }

    blunt_decoder_destroy(dec);
    blunt_frame_free(&ycbcr_in);
    blunt_frame_free(&ycbcr_out);
    return 0;
}
