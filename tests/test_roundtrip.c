#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "blunt.h"

static void generate_test_frame(uint8_t *rgb, int w, int h, int frame_num,
                                int pattern) {
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            int off = (y * w + x) * 3;
            switch (pattern % 4) {
            case 0: /* Gradient */
                rgb[off + 0] = (uint8_t)((x * 255) / w);
                rgb[off + 1] = (uint8_t)((y * 255) / h);
                rgb[off + 2] = (uint8_t)(((x + y) * 255) / (w + h));
                break;
            case 1: /* Moving bar */
                rgb[off + 0] = (uint8_t)((abs((x + frame_num * 3) % w - w/2) < w/8) ? 255 : 0);
                rgb[off + 1] = (uint8_t)((abs((y + frame_num * 2) % h - h/2) < h/8) ? 255 : 0);
                rgb[off + 2] = 128;
                break;
            case 2: /* Checkerboard */
                rgb[off + 0] = (((x / 8) + (y / 8) + frame_num) & 1) ? 255 : 0;
                rgb[off + 1] = rgb[off + 0];
                rgb[off + 2] = rgb[off + 0];
                break;
            case 3: /* Color bars */
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
    const int NUM_FRAMES = 10;
    const char *TEST_FILE = "test_output.blunt";

    printf("=== BLUNT Codec Roundtrip Test ===\n");
    printf("Resolution: %dx%d, Frames: %d\n\n", W, H, NUM_FRAMES);

    /* --- ENCODE --- */
    printf("[1] Encoding %d test frames...\n", NUM_FRAMES);

    uint8_t *rgb_in = (uint8_t *)malloc(W * H * 3);
    uint8_t *rgb_out = (uint8_t *)malloc(W * H * 3);
    BluntFrame ycbcr_in, ycbcr_out;
    memset(&ycbcr_in, 0, sizeof(ycbcr_in));
    memset(&ycbcr_out, 0, sizeof(ycbcr_out));

    BluntEncoder *enc = blunt_encoder_create();
    blunt_encoder_set_quality(enc, 80);

    if (blunt_encoder_open(enc, TEST_FILE, W, H, 24, 1) != 0) {
        fprintf(stderr, "FAIL: Cannot open encoder\n");
        return 1;
    }

    for (int i = 0; i < NUM_FRAMES; i++) {
        generate_test_frame(rgb_in, W, H, i, i);
        blunt_rgb_to_ycbcr(rgb_in, W, H, W * 3, &ycbcr_in);
        blunt_encoder_write_frame(enc, &ycbcr_in, 1);
        printf("  Encoded frame %d/%d\n", i + 1, NUM_FRAMES);
    }

    blunt_encoder_close(enc);
    blunt_encoder_destroy(enc);
    printf("  File written: %s\n\n", TEST_FILE);

    /* --- DECODE --- */
    printf("[2] Decoding frames...\n");

    BluntDecoder *dec = blunt_decoder_create();
    if (blunt_decoder_open(dec, TEST_FILE) != 0) {
        fprintf(stderr, "FAIL: Cannot open decoder\n");
        return 1;
    }

    BluntHeader hdr;
    blunt_decoder_read_header(dec, &hdr);
    printf("  Header OK: %dx%d, %u frames\n", hdr.width, hdr.height, hdr.num_frames);

    int errors = 0;
    for (uint32_t i = 0; i < hdr.num_frames; i++) {
        if (blunt_decoder_read_frame(dec, &ycbcr_out) != 0) {
            fprintf(stderr, "  FAIL: Cannot decode frame %u\n", i);
            errors++;
            continue;
        }

        generate_test_frame(rgb_in, W, H, i, i);
        blunt_rgb_to_ycbcr(rgb_in, W, H, W * 3, &ycbcr_in);

        double mse = 0;
        int y_count = ycbcr_out.y_stride * hdr.height;
        int c_count = ycbcr_out.cb_stride * ((hdr.height + 1) / 2);
        for (int j = 0; j < y_count; j++) {
            double diff = (double)ycbcr_in.y[j] - (double)ycbcr_out.y[j];
            mse += diff * diff;
        }
        for (int j = 0; j < c_count; j++) {
            double diff = (double)ycbcr_in.cb[j] - (double)ycbcr_out.cb[j];
            mse += diff * diff;
            diff = (double)ycbcr_in.cr[j] - (double)ycbcr_out.cr[j];
            mse += diff * diff;
        }
        int total = y_count + c_count * 2;
        mse /= total;
        double psnr = (mse > 0) ? 10.0 * log10(255.0 * 255.0 / mse) : 99.99;

        printf("  Frame %u: YCbCr PSNR = %.2f dB %s\n", i, psnr,
               (psnr > 20.0) ? "OK" : "LOW");
        if (psnr < 20.0) errors++;
    }

    blunt_decoder_destroy(dec);
    blunt_frame_free(&ycbcr_in);
    blunt_frame_free(&ycbcr_out);
    free(rgb_in);
    free(rgb_out);

    printf("\n[3] Result: %s (%d errors)\n",
           errors == 0 ? "PASS" : "FAIL", errors);

    return errors;
}
