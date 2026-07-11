#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "blunt.h"

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: blunt_info <file.blunt>\n");
        return 1;
    }

    FILE *fp = fopen(argv[1], "rb");
    if (!fp) {
        fprintf(stderr, "Error: Cannot open %s\n", argv[1]);
        return 1;
    }

    BluntHeader hdr;
    if (fread(&hdr, sizeof(hdr), 1, fp) != 1) {
        fprintf(stderr, "Error: Cannot read header\n");
        fclose(fp);
        return 1;
    }

    if (memcmp(hdr.magic, BLUNT_MAGIC, 4) != 0) {
        fprintf(stderr, "Error: Not a BLUNT file (magic: %.4s)\n", hdr.magic);
        fclose(fp);
        return 1;
    }

    printf("=== BLUNT File Information ===\n");
    printf("File:          %s\n", argv[1]);
    printf("Magic:         %.4s\n", hdr.magic);
    printf("Version:       %d\n", hdr.version);
    printf("Header size:   %u bytes\n", hdr.header_size);
    printf("Resolution:    %dx%d\n", hdr.width, hdr.height);
    printf("Macroblocks:   %dx%d\n", hdr.mb_width, hdr.mb_height);
    printf("Frame rate:    %d/%d fps\n", hdr.fps_num, hdr.fps_den);
    printf("Total frames:  %u\n", hdr.num_frames);
    printf("Keyframes:     %u\n", hdr.num_keyframes);
    printf("Quality:       %d/100\n", hdr.quality);
    printf("Flags:         0x%02X", hdr.flags);
    if (hdr.flags & BLUNT_FLAG_ALPHA) printf(" [alpha]");
    if (hdr.flags & BLUNT_FLAG_INTERLACE) printf(" [interlaced]");
    printf("\n");

    double duration = 0;
    if (hdr.fps_num > 0)
        duration = (double)hdr.num_frames * hdr.fps_den / hdr.fps_num;
    printf("Duration:      %.2f seconds\n", duration);

    /* File size */
    fseek(fp, 0, SEEK_END);
    long file_size = ftell(fp);
    printf("File size:     %ld bytes", file_size);
    if (file_size > 1024 * 1024)
        printf(" (%.2f MB)", file_size / (1024.0 * 1024.0));
    else if (file_size > 1024)
        printf(" (%.2f KB)", file_size / 1024.0);
    printf("\n");

    /* Scan frames */
    fseek(fp, BLUNT_HEADER_SIZE, SEEK_SET);
    int i_frames = 0, p_frames = 0;
    double total_frame_bits = 0;

    for (uint32_t i = 0; i < hdr.num_frames; i++) {
        BluntFrameHeader fhdr;
        if (fread(&fhdr, sizeof(fhdr), 1, fp) != 1) break;

        if (fhdr.frame_type == BLUNT_FRAME_I) i_frames++;
        else p_frames++;

        total_frame_bits += fhdr.data_size * 8;

        fseek(fp, fhdr.data_size, SEEK_CUR);
    }

    printf("I-frames:      %d\n", i_frames);
    printf("P-frames:      %d\n", p_frames);
    if (hdr.num_frames > 0)
        printf("Avg frame:     %.1f bytes\n", total_frame_bits / 8.0 / hdr.num_frames);
    if (duration > 0)
        printf("Avg bitrate:   %.1f kbps\n", total_frame_bits / 1000.0 / duration);

    fclose(fp);
    return 0;
}
