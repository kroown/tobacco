#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "blunt.h"

static void generate_test_frame(uint8_t *rgb, int w, int h, int frame_num, int pattern) {
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            int off = (y * w + x) * 3;
            switch (pattern % 4) {
            case 0:
                rgb[off + 0] = (uint8_t)((x * 255) / w);
                rgb[off + 1] = (uint8_t)((y * 255) / h);
                rgb[off + 2] = (uint8_t)(((x + y) * 255) / (w + h));
                break;
            case 1:
                rgb[off + 0] = (uint8_t)((abs((x + frame_num * 3) % w - w/2) < w/8) ? 255 : 0);
                rgb[off + 1] = (uint8_t)((abs((y + frame_num * 2) % h - h/2) < h/8) ? 255 : 0);
                rgb[off + 2] = 128;
                break;
            case 2:
                rgb[off + 0] = (((x / 8) + (y / 8) + frame_num) & 1) ? 255 : 0;
                rgb[off + 1] = rgb[off + 0];
                rgb[off + 2] = rgb[off + 0];
                break;
            case 3:
                rgb[off + 0] = (uint8_t)((x < w/4) ? 255 : (x < w/2) ? 0 : (x < 3*w/4) ? 0 : 255);
                rgb[off + 1] = (uint8_t)((x < w/4) ? 0 : (x < w/2) ? 255 : (x < 3*w/4) ? 0 : 255);
                rgb[off + 2] = (uint8_t)((x < w/4) ? 0 : (x < w/2) ? 0 : (x < 3*w/4) ? 255 : 0);
                break;
            }
        }
    }
}

int main(void) {
    const int W = 320;
    const int H = 240;

    uint8_t *rgb_in = (uint8_t *)malloc(W * H * 3);
    BluntFrame ycbcr_in, ycbcr_out;
    memset(&ycbcr_in, 0, sizeof(ycbcr_in));
    memset(&ycbcr_out, 0, sizeof(ycbcr_out));

    /* Encode and decode just pattern 1 (moving bar) */
    generate_test_frame(rgb_in, W, H, 1, 1);
    blunt_rgb_to_ycbcr(rgb_in, W, H, W * 3, &ycbcr_in);

    printf("=== Moving Bar Pattern I-Frame Debug ===\n");
    printf("Y range: min=%d max=%d\n",
           ycbcr_in.y[0], ycbcr_in.y[0]);
    {
        int ymin = 255, ymax = 0;
        for (int j = 0; j < ycbcr_in.y_stride * H; j++) {
            if (ycbcr_in.y[j] < ymin) ymin = ycbcr_in.y[j];
            if (ycbcr_in.y[j] > ymax) ymax = ycbcr_in.y[j];
        }
        printf("Y range: min=%d max=%d\n", ymin, ymax);
    }

    BluntEncoder *enc = blunt_encoder_create();
    blunt_encoder_set_quality(enc, 80);
    blunt_encoder_open(enc, "debug_bar.blunt", W, H, 24, 1);
    blunt_encoder_write_frame(enc, &ycbcr_in, 1);
    blunt_encoder_close(enc);
    blunt_encoder_destroy(enc);

    BluntDecoder *dec = blunt_decoder_create();
    blunt_decoder_open(dec, "debug_bar.blunt");
    blunt_decoder_read_frame(dec, &ycbcr_out);

    /* Per-plane PSNR */
    double mse_y = 0, mse_cb = 0, mse_cr = 0;
    int yc = ycbcr_in.y_stride * H;
    int cc = ycbcr_in.cb_stride * (H/2);
    for (int j = 0; j < yc; j++) {
        double d = (double)ycbcr_in.y[j] - (double)ycbcr_out.y[j];
        mse_y += d * d;
    }
    for (int j = 0; j < cc; j++) {
        double d = (double)ycbcr_in.cb[j] - (double)ycbcr_out.cb[j];
        mse_cb += d * d;
        d = (double)ycbcr_in.cr[j] - (double)ycbcr_out.cr[j];
        mse_cr += d * d;
    }
    mse_y /= yc; mse_cb /= cc; mse_cr /= cc;
    printf("Y  PSNR: %.2f dB\n", mse_y > 0 ? 10.0*log10(255.0*255.0/mse_y) : 99.99);
    printf("Cb PSNR: %.2f dB\n", mse_cb > 0 ? 10.0*log10(255.0*255.0/mse_cb) : 99.99);
    printf("Cr PSNR: %.2f dB\n", mse_cr > 0 ? 10.0*log10(255.0*255.0/mse_cr) : 99.99);

    /* Show some sample values around the edge */
    printf("\nY plane row 118-122, col 138-142 (around bar edge):\n");
    for (int y = 118; y <= 122; y++) {
        printf("  row %d: orig=", y);
        for (int x = 138; x <= 142; x++)
            printf("%3d ", ycbcr_in.y[y * ycbcr_in.y_stride + x]);
        printf(" dec=");
        for (int x = 138; x <= 142; x++)
            printf("%3d ", ycbcr_out.y[y * ycbcr_out.y_stride + x]);
        printf("\n");
    }

    blunt_decoder_destroy(dec);
    blunt_frame_free(&ycbcr_in);
    blunt_frame_free(&ycbcr_out);
    free(rgb_in);
    return 0;
}
