#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "blunt.h"

static void progress_cb(int cur, int total, void *ud) {
    (void)ud;
    fprintf(stderr, "\rDecoding frame %d / %d", cur, total);
}

static int write_ppm(const char *path, const uint8_t *rgb, int w, int h) {
    FILE *fp = fopen(path, "wb");
    if (!fp) return -1;
    fprintf(fp, "P6\n%d %d\n255\n", w, h);
    fwrite(rgb, 1, w * h * 3, fp);
    fclose(fp);
    return 0;
}

static int write_frame_pgm(const char *path, const uint8_t *plane, int w, int h, int stride) {
    char name[256];
    snprintf(name, sizeof(name), "%s", path);
    FILE *fp = fopen(name, "wb");
    if (!fp) return -1;
    fprintf(fp, "P5\n%d %d\n255\n", w, h);
    for (int y = 0; y < h; y++)
        fwrite(plane + y * stride, 1, w, fp);
    fclose(fp);
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: blunt_decode <input.blunt> [output_prefix]\n");
        fprintf(stderr, "  Outputs .ppm frames (e.g., output_0000.ppm)\n");
        return 1;
    }

    BluntDecoder *dec = blunt_decoder_create();
    if (!dec) {
        fprintf(stderr, "Error: Failed to create decoder\n");
        return 1;
    }

    if (blunt_decoder_open(dec, argv[1]) != 0) {
        fprintf(stderr, "Error: Cannot open %s\n", argv[1]);
        blunt_decoder_destroy(dec);
        return 1;
    }

    BluntHeader hdr;
    blunt_decoder_read_header(dec, &hdr);

    fprintf(stderr, "BLUNT file: %s\n", argv[1]);
    fprintf(stderr, "  Version: %d\n", hdr.version);
    fprintf(stderr, "  Resolution: %dx%d\n", hdr.width, hdr.height);
    fprintf(stderr, "  FPS: %d/%d\n", hdr.fps_num, hdr.fps_den);
    fprintf(stderr, "  Frames: %u\n", hdr.num_frames);
    fprintf(stderr, "  Quality: %d\n", hdr.quality);
    fprintf(stderr, "  SIMD: %s\n\n", blunt_version_string());

    const char *prefix = "output";
    if (argc >= 3)
        prefix = argv[2];

    blunt_decoder_set_progress(dec, progress_cb, NULL);

    BluntFrame frame;
    memset(&frame, 0, sizeof(frame));

    for (uint32_t i = 0; i < hdr.num_frames; i++) {
        if (blunt_decoder_read_frame(dec, &frame) != 0) {
            fprintf(stderr, "\nError decoding frame %u\n", i);
            break;
        }

        char name[256];
        snprintf(name, sizeof(name), "%s_%04d.ppm", prefix, i);

        uint8_t *rgb = (uint8_t *)malloc(hdr.width * hdr.height * 3);
        if (rgb) {
            blunt_ycbcr_to_rgb(&frame, rgb, hdr.width, hdr.height, hdr.width * 3);
            write_ppm(name, rgb, hdr.width, hdr.height);
            free(rgb);
        }

        fprintf(stderr, "\rDecoded frame %u / %u", i + 1, hdr.num_frames);
    }

    fprintf(stderr, "\n\nDone. Output written as %s_NNNN.ppm\n", prefix);

    blunt_frame_free(&frame);
    blunt_decoder_destroy(dec);
    return 0;
}
