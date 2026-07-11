#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "blunt.h"

int main(void) {
    const int W = 320;
    const int H = 240;
    printf("=== I-Only vs I+P Frame Test ===\n\n");

    uint8_t *rgb_in = (uint8_t *)malloc(W * H * 3);
    BluntFrame ycbcr_in, ycbcr_out;
    memset(&ycbcr_in, 0, sizeof(ycbcr_in));
    memset(&ycbcr_out, 0, sizeof(ycbcr_out));

    /* Test 1: All I-frames */
    printf("[Test 1: All I-frames (every frame forced keyframe)]\n");
    BluntEncoder *enc = blunt_encoder_create();
    blunt_encoder_set_quality(enc, 80);
    blunt_encoder_open(enc, "test_iall.blunt", W, H, 24, 1);
    for (int i = 0; i < 3; i++) {
        for (int y = 0; y < H; y++)
            for (int x = 0; x < W; x++) {
                int off = (y * W + x) * 3;
                rgb_in[off + 0] = (uint8_t)((x * 255) / W);
                rgb_in[off + 1] = (uint8_t)((y * 255) / H);
                rgb_in[off + 2] = (uint8_t)(((x + y) * 255) / (W + H));
            }
        blunt_rgb_to_ycbcr(rgb_in, W, H, W * 3, &ycbcr_in);
        blunt_encoder_write_frame(enc, &ycbcr_in, 1);
    }
    blunt_encoder_close(enc);
    blunt_encoder_destroy(enc);

    BluntDecoder *dec = blunt_decoder_create();
    blunt_decoder_open(dec, "test_iall.blunt");
    for (int i = 0; i < 3; i++) {
        blunt_decoder_read_frame(dec, &ycbcr_out);
        for (int y = 0; y < H; y++)
            for (int x = 0; x < W; x++) {
                int off = (y * W + x) * 3;
                rgb_in[off + 0] = (uint8_t)((x * 255) / W);
                rgb_in[off + 1] = (uint8_t)((y * 255) / H);
                rgb_in[off + 2] = (uint8_t)(((x + y) * 255) / (W + H));
            }
        blunt_rgb_to_ycbcr(rgb_in, W, H, W * 3, &ycbcr_in);
        double mse = 0;
        int total = ycbcr_out.y_stride * H + ycbcr_out.cb_stride * (H/2) * 2 + ycbcr_out.cr_stride * (H/2) * 2;
        for (int j = 0; j < ycbcr_out.y_stride * H; j++)
            mse += (double)((int)ycbcr_in.y[j] - (int)ycbcr_out.y[j]) * ((int)ycbcr_in.y[j] - (int)ycbcr_out.y[j]);
        for (int j = 0; j < ycbcr_out.cb_stride * (H/2); j++) {
            mse += (double)((int)ycbcr_in.cb[j] - (int)ycbcr_out.cb[j]) * ((int)ycbcr_in.cb[j] - (int)ycbcr_out.cb[j]);
            mse += (double)((int)ycbcr_in.cr[j] - (int)ycbcr_out.cr[j]) * ((int)ycbcr_in.cr[j] - (int)ycbcr_out.cr[j]);
        }
        mse /= total;
        printf("  Frame %d (I): YCbCr PSNR = %.2f dB\n", i,
               mse > 0 ? 10.0*log10(255.0*255.0/mse) : 99.99);
    }
    blunt_decoder_destroy(dec);

    /* Test 2: I + P frames with matching reference */
    printf("\n[Test 2: I + P frames, encoder uses same reference as decoder]\n");
    enc = blunt_encoder_create();
    blunt_encoder_set_quality(enc, 80);
    blunt_encoder_open(enc, "test_ip.blunt", W, H, 24, 1);
    for (int i = 0; i < 3; i++) {
        int pattern = i;
        for (int y = 0; y < H; y++)
            for (int x = 0; x < W; x++) {
                int off = (y * W + x) * 3;
                switch (pattern % 4) {
                case 0:
                    rgb_in[off + 0] = (uint8_t)((x * 255) / W);
                    rgb_in[off + 1] = (uint8_t)((y * 255) / H);
                    rgb_in[off + 2] = (uint8_t)(((x + y) * 255) / (W + H));
                    break;
                case 1:
                    rgb_in[off + 0] = (uint8_t)((abs((x + 3) % W - W/2) < W/8) ? 255 : 0);
                    rgb_in[off + 1] = (uint8_t)((abs((y + 2) % H - H/2) < H/8) ? 255 : 0);
                    rgb_in[off + 2] = 128;
                    break;
                case 2:
                    rgb_in[off + 0] = (((x / 8) + (y / 8) + i) & 1) ? 255 : 0;
                    rgb_in[off + 1] = rgb_in[off + 0];
                    rgb_in[off + 2] = rgb_in[off + 0];
                    break;
                default:
                    rgb_in[off + 0] = 128;
                    rgb_in[off + 1] = 128;
                    rgb_in[off + 2] = 128;
                }
            }
        blunt_rgb_to_ycbcr(rgb_in, W, H, W * 3, &ycbcr_in);
        blunt_encoder_write_frame(enc, &ycbcr_in, i == 0);
    }
    blunt_encoder_close(enc);
    blunt_encoder_destroy(enc);

    dec = blunt_decoder_create();
    blunt_decoder_open(dec, "test_ip.blunt");
    for (int i = 0; i < 3; i++) {
        blunt_decoder_read_frame(dec, &ycbcr_out);

        /* Regenerate same pattern for comparison */
        int pattern = i;
        for (int y = 0; y < H; y++)
            for (int x = 0; x < W; x++) {
                int off = (y * W + x) * 3;
                switch (pattern % 4) {
                case 0:
                    rgb_in[off + 0] = (uint8_t)((x * 255) / W);
                    rgb_in[off + 1] = (uint8_t)((y * 255) / H);
                    rgb_in[off + 2] = (uint8_t)(((x + y) * 255) / (W + H));
                    break;
                case 1:
                    rgb_in[off + 0] = (uint8_t)((abs((x + 3) % W - W/2) < W/8) ? 255 : 0);
                    rgb_in[off + 1] = (uint8_t)((abs((y + 2) % H - H/2) < H/8) ? 255 : 0);
                    rgb_in[off + 2] = 128;
                    break;
                case 2:
                    rgb_in[off + 0] = (((x / 8) + (y / 8) + i) & 1) ? 255 : 0;
                    rgb_in[off + 1] = rgb_in[off + 0];
                    rgb_in[off + 2] = rgb_in[off + 0];
                    break;
                default:
                    rgb_in[off + 0] = 128;
                    rgb_in[off + 1] = 128;
                    rgb_in[off + 2] = 128;
                }
            }
        blunt_rgb_to_ycbcr(rgb_in, W, H, W * 3, &ycbcr_in);

        double mse = 0;
        int total = ycbcr_out.y_stride * H + ycbcr_out.cb_stride * (H/2) * 2 + ycbcr_out.cr_stride * (H/2) * 2;
        for (int j = 0; j < ycbcr_out.y_stride * H; j++)
            mse += (double)((int)ycbcr_in.y[j] - (int)ycbcr_out.y[j]) * ((int)ycbcr_in.y[j] - (int)ycbcr_out.y[j]);
        for (int j = 0; j < ycbcr_out.cb_stride * (H/2); j++) {
            mse += (double)((int)ycbcr_in.cb[j] - (int)ycbcr_out.cb[j]) * ((int)ycbcr_in.cb[j] - (int)ycbcr_out.cb[j]);
            mse += (double)((int)ycbcr_in.cr[j] - (int)ycbcr_out.cr[j]) * ((int)ycbcr_in.cr[j] - (int)ycbcr_out.cr[j]);
        }
        mse /= total;
        printf("  Frame %d (%c): YCbCr PSNR = %.2f dB\n", i,
               i == 0 ? 'I' : 'P',
               mse > 0 ? 10.0*log10(255.0*255.0/mse) : 99.99);
    }
    blunt_decoder_destroy(dec);

    free(rgb_in);
    blunt_frame_free(&ycbcr_in);
    blunt_frame_free(&ycbcr_out);
    return 0;
}
