#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "blunt.h"

static uint8_t *read_ppm(const char *path, int *w, int *h) {
    FILE *fp = fopen(path, "rb");
    if (!fp) return NULL;

    char magic[4];
    if (fscanf(fp, "%3s", magic) != 1 || strcmp(magic, "P6") != 0) {
        fclose(fp);
        return NULL;
    }

    /* Skip comments */
    int c;
    while ((c = fgetc(fp)) == '#')
        while (fgetc(fp) != '\n');

    ungetc(c, fp);
    fscanf(fp, "%d %d", w, h);
    int maxval;
    fscanf(fp, "%d", &maxval);
    fgetc(fp); /* consume whitespace */

    size_t size = (size_t)(*w) * (*h) * 3;
    uint8_t *data = (uint8_t *)malloc(size);
    if (data)
        fread(data, 1, size, fp);

    fclose(fp);
    return data;
}

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "Usage: blunt_encode <input_NNNN.ppm> <output.blunt> [fps] [quality]\n");
        fprintf(stderr, "  Encodes PPM frames into a .blunt file.\n");
        fprintf(stderr, "  Frames should be named input_0000.ppm, input_0001.ppm, etc.\n");
        fprintf(stderr, "  Use '%%d' in the input path for frame numbering.\n");
        return 1;
    }

    const char *input_pattern = argv[1];
    const char *output_path = argv[2];
    int fps_num = 24, fps_den = 1;
    int quality = 75;

    if (argc >= 4) {
        if (sscanf(argv[3], "%d/%d", &fps_num, &fps_den) != 2)
            fps_num = atoi(argv[3]);
        if (fps_num < 1) fps_num = 1;
        if (fps_den < 1) fps_den = 1;
    }
    if (argc >= 5)
        quality = atoi(argv[4]);
    if (quality < 1) quality = 1;
    if (quality > 100) quality = 100;

    /* Count frames */
    int total_frames = 0;
    for (int i = 0; i < 10000; i++) {
        char name[512];
        /* Try to find the frame using the pattern with integer substitution */
        snprintf(name, sizeof(name), "%s", input_pattern);

        /* Replace %d or %04d etc with actual number */
        char numbered[512];
        int found = 0;
        const char *pct = strchr(name, '%');
        if (pct) {
            snprintf(numbered, sizeof(numbered), "%s", name);
            char fmt[64];
            snprintf(fmt, sizeof(fmt), "%%%s", pct + 1);
            /* Find end of format specifier */
            char *end = numbered + (pct - name);
            /* Overwrite in the actual format string */
            snprintf(numbered, sizeof(numbered), "%.*s", (int)(pct - name), name);
            char numstr[32];
            snprintf(numstr, sizeof(numstr), fmt, i);
            strcat(numbered, numstr);
            /* Append rest after format spec */
            const char *spec_end = pct + 1;
            while (*spec_end && *spec_end != 's' && *spec_end != 'd' &&
                   *spec_end != 'i' && *spec_end != 'u' && *spec_end != 'x')
                spec_end++;
            if (*spec_end) spec_end++;
            strcat(numbered, spec_end);
        } else {
            snprintf(numbered, sizeof(numbered), "%s_%04d.ppm", name, i);
        }

        FILE *test = fopen(numbered, "rb");
        if (!test) break;
        fclose(test);
        total_frames++;
    }

    if (total_frames == 0) {
        /* Try numbered pattern directly */
        for (int i = 0; i < 10000; i++) {
            char name[512];
            snprintf(name, sizeof(name), "%s_%04d.ppm", input_pattern, i);
            FILE *test = fopen(name, "rb");
            if (!test) break;
            fclose(test);
            total_frames++;
        }
    }

    if (total_frames == 0) {
        fprintf(stderr, "Error: No input frames found matching pattern '%s'\n", input_pattern);
        return 1;
    }

    fprintf(stderr, "Encoding %d frames at %d/%d fps, quality %d\n",
            total_frames, fps_num, fps_den, quality);

    /* Read first frame to get dimensions */
    char first[512];
    snprintf(first, sizeof(first), "%s_0000.ppm", input_pattern);
    int w = 0, h = 0;
    uint8_t *rgb = read_ppm(first, &w, &h);
    if (!rgb) {
        /* Try without _NNNN suffix */
        snprintf(first, sizeof(first), "%s", input_pattern);
        rgb = read_ppm(first, &w, &h);
    }
    if (!rgb) {
        fprintf(stderr, "Error: Cannot read first frame\n");
        return 1;
    }

    BluntEncoder *enc = blunt_encoder_create();
    if (!enc) {
        fprintf(stderr, "Error: Failed to create encoder\n");
        free(rgb);
        return 1;
    }

    blunt_encoder_set_quality(enc, (uint8_t)quality);

    if (blunt_encoder_open(enc, output_path, (uint16_t)w, (uint16_t)h,
                           (uint16_t)fps_num, (uint16_t)fps_den) != 0) {
        fprintf(stderr, "Error: Cannot open output file %s\n", output_path);
        blunt_encoder_destroy(enc);
        free(rgb);
        return 1;
    }

    fprintf(stderr, "  Resolution: %dx%d\n", w, h);

    BluntFrame ycbcr;
    memset(&ycbcr, 0, sizeof(ycbcr));

    for (int i = 0; i < total_frames; i++) {
        char name[512];
        snprintf(name, sizeof(name), "%s_%04d.ppm", input_pattern, i);

        if (i > 0) {
            free(rgb);
            rgb = read_ppm(name, &w, &h);
            if (!rgb) {
                fprintf(stderr, "\nError reading frame %d: %s\n", i, name);
                break;
            }
        }

        blunt_rgb_to_ycbcr(rgb, w, h, w * 3, &ycbcr);
        blunt_encoder_write_frame(enc, &ycbcr, i == 0);

        fprintf(stderr, "\rEncoded frame %d / %d", i + 1, total_frames);
    }

    fprintf(stderr, "\n");
    blunt_encoder_close(enc);
    blunt_frame_free(&ycbcr);
    blunt_encoder_destroy(enc);
    free(rgb);

    fprintf(stderr, "Output: %s\n", output_path);
    return 0;
}
