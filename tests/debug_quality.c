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

static double compute_ycbcr_psnr(const BluntFrame *a, const BluntFrame *b, int w, int h) {
    double mse = 0;
    int y_count = a->y_stride * h;
    int c_count = a->cb_stride * ((h + 1) / 2);
    for (int j = 0; j < y_count; j++) {
        double diff = (double)a->y[j] - (double)b->y[j];
        mse += diff * diff;
    }
    for (int j = 0; j < c_count; j++) {
        double diff = (double)a->cb[j] - (double)b->cb[j];
        mse += diff * diff;
        diff = (double)a->cr[j] - (double)b->cr[j];
        mse += diff * diff;
    }
    mse /= (y_count + c_count * 2);
    return (mse > 0) ? 10.0 * log10(255.0 * 255.0 / mse) : 99.99;
}

static const char *pattern_names[] = {"Gradient", "MovingBar", "Checkerboard", "ColorBars"};

int main(void) {
    const int W = 320, H = 240;
    int qualities[] = {80, 90, 95, 98, 100};

    printf("Quality  Gradient  MovingBar  Checker  ColorBars  Min\n");
    printf("-------  --------  ---------  -------  ---------  ---\n");

    for (int qi = 0; qi < 5; qi++) {
        int quality = qualities[qi];
        double psnrs[4];

        for (int pat = 0; pat < 4; pat++) {
            uint8_t *rgb_in = (uint8_t *)malloc(W * H * 3);
            BluntFrame ycbcr_in, ycbcr_out;
            memset(&ycbcr_in, 0, sizeof(ycbcr_in));
            memset(&ycbcr_out, 0, sizeof(ycbcr_out));

            char fname[64];
            sprintf(fname, "test_q%d_p%d.blunt", quality, pat);

            BluntEncoder *enc = blunt_encoder_create();
            blunt_encoder_set_quality(enc, quality);
            blunt_encoder_open(enc, fname, W, H, 24, 1);

            generate_test_frame(rgb_in, W, H, 0, pat);
            blunt_rgb_to_ycbcr(rgb_in, W, H, W * 3, &ycbcr_in);
            blunt_encoder_write_frame(enc, &ycbcr_in, 1);
            blunt_encoder_close(enc);
            blunt_encoder_destroy(enc);

            BluntDecoder *dec = blunt_decoder_create();
            blunt_decoder_open(dec, fname);
            blunt_decoder_read_frame(dec, &ycbcr_out);
            blunt_decoder_destroy(dec);

            psnrs[pat] = compute_ycbcr_psnr(&ycbcr_in, &ycbcr_out, W, H);

            blunt_frame_free(&ycbcr_in);
            blunt_frame_free(&ycbcr_out);
            free(rgb_in);
        }

        double min_psnr = psnrs[0];
        for (int p = 1; p < 4; p++)
            if (psnrs[p] < min_psnr) min_psnr = psnrs[p];

        printf("%7d  %8.2f  %9.2f  %7.2f  %9.2f  %6.2f\n",
               quality, psnrs[0], psnrs[1], psnrs[2], psnrs[3], min_psnr);
    }
    return 0;
}
