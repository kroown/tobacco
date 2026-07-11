#include "blunt.h"
#include "blunt_audio.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(const char *prog) {
    fprintf(stderr,
        "Usage: %s [options]\n"
        "  WAV to BLUNT:  %s -i input.wav -o output.blunt [options]\n"
        "  BLUNT to WAV:  %s -i input.blunt -o output.wav\n"
        "\nOptions:\n"
        "  -i <file>      input file (.wav or .blunt)\n"
        "  -o <file>      output file (.blunt or .wav)\n"
        "  -q <1-100>     quality for encoding (default 75)\n"
        "  -w <width>     video width for blank video (default 320)\n"
        "  -h <height>    video height for blank video (default 240)\n"
        "  -f <fps>       frame rate (default 30)\n",
        prog, prog, prog);
}

static int is_blunt(const char *path) {
    FILE *fp = fopen(path, "rb");
    if (!fp) return 0;
    char magic[4];
    int ok = (fread(magic, 1, 4, fp) == 4 && memcmp(magic, "BLNT", 4) == 0);
    fclose(fp);
    return ok;
}

static int wav_to_blunt(const char *in_wav, const char *out_blunt,
                         int quality, int width, int height, int fps) {
    int channels, sample_rate, bits, data_offset, data_size;
    if (blunt_wav_read_header(in_wav, &channels, &sample_rate, &bits,
                              &data_offset, &data_size) != 0) {
        fprintf(stderr, "Error: cannot read WAV header from %s\n", in_wav);
        return 1;
    }

    printf("WAV: %d Hz, %d-bit, %d channels\n", sample_rate, bits, channels);
    printf("Data offset: %d, size: %d bytes\n", data_offset, data_size);

    int total_samples = data_size / (bits / 8);
    int16_t *pcm = (int16_t *)malloc(total_samples * sizeof(int16_t));
    if (!pcm) { fprintf(stderr, "Error: out of memory\n"); return 1; }

    if (bits == 16) {
        blunt_wav_read_samples(in_wav, data_offset, pcm, total_samples);
    } else if (bits == 8) {
        uint8_t *u8 = (uint8_t *)malloc(data_size);
        blunt_wav_read_samples(in_wav, data_offset, (int16_t *)u8, data_size);
        for (int i = 0; i < data_size; i++)
            pcm[i] = (int16_t)((u8[i] - 128) << 8);
        free(u8);
        total_samples = data_size;
    } else {
        fprintf(stderr, "Error: unsupported bit depth %d\n", bits);
        free(pcm);
        return 1;
    }

    int samples_per_frame = sample_rate / fps;
    int total_frames = total_samples / (samples_per_frame * channels);
    if (total_frames < 1) total_frames = 1;

    printf("Creating %dx%d @ %d fps, %d frames\n", width, height, fps, total_frames);

    BluntEncoder *enc = blunt_encoder_create();
    if (!enc) { free(pcm); return 1; }
    blunt_encoder_set_quality(enc, quality);
    blunt_encoder_set_audio(enc, channels, sample_rate);
    if (blunt_encoder_open(enc, out_blunt, width, height, fps, 1) != 0) {
        fprintf(stderr, "Error: cannot open output %s\n", out_blunt);
        free(pcm); blunt_encoder_destroy(enc); return 1;
    }

    /* Allocate blank video frame */
    BluntFrame vframe;
    int mbw = (width + 15) / 16;
    int mbh = (height + 15) / 16;
    blunt_frame_alloc(&vframe, mbw, mbh);
    memset(vframe.y, 16, vframe.y_stride * height);
    memset(vframe.cb, 128, vframe.cb_stride * ((height + 1) / 2));
    memset(vframe.cr, 128, vframe.cr_stride * ((height + 1) / 2));

    int sample_idx = 0;
    for (int f = 0; f < total_frames; f++) {
        blunt_encoder_write_frame(enc, &vframe, f == 0);

        int remain = total_samples - sample_idx;
        int want = samples_per_frame * channels;
        if (remain < want) want = remain;
        if (want > 0)
            blunt_encoder_write_audio_frame(enc, pcm + sample_idx, want);
        sample_idx += want;

        if (f % 10 == 0)
            printf("\rFrame %d/%d", f + 1, total_frames);
    }
    printf("\rDone: %d frames written\n", total_frames);

    blunt_frame_free(&vframe);
    blunt_encoder_close(enc);
    blunt_encoder_destroy(enc);
    free(pcm);
    return 0;
}

static int blunt_to_wav(const char *in_blunt, const char *out_wav) {
    BluntDecoder *dec = blunt_decoder_create();
    if (!dec) return 1;

    if (blunt_decoder_open(dec, in_blunt) != 0) {
        fprintf(stderr, "Error: cannot open %s\n", in_blunt);
        blunt_decoder_destroy(dec);
        return 1;
    }

    BluntHeader hdr;
    blunt_decoder_read_header(dec, &hdr);

    if (!(hdr.flags & BLUNT_FLAG_HAS_AUDIO)) {
        fprintf(stderr, "Error: .blunt file has no audio track\n");
        blunt_decoder_destroy(dec);
        return 1;
    }

    int channels = hdr.audio_channels;
    int sample_rate = hdr.audio_sample_rate;
    int bits = hdr.audio_bits;
    if (channels <= 0 || sample_rate <= 0) {
        fprintf(stderr, "Error: invalid audio params in header\n");
        blunt_decoder_destroy(dec);
        return 1;
    }

    printf("Audio: %d Hz, %d-bit, %d channels\n", sample_rate, bits, channels);
    printf("Frames: %d\n", hdr.num_frames);

    int samples_per_frame = sample_rate * hdr.fps_den / hdr.fps_num;
    int max_per_frame = samples_per_frame * channels;
    int16_t *frame_buf = (int16_t *)malloc(max_per_frame * sizeof(int16_t));
    int16_t *all_pcm = NULL;
    int total_alloc = 0;
    int total_written = 0;

    BluntFrame vf;
    int mbw = hdr.mb_width, mbh = hdr.mb_height;
    blunt_frame_alloc(&vf, mbw, mbh);

    for (uint32_t f = 0; f < hdr.num_frames; f++) {
        /* Read video frame to advance file position */
        blunt_decoder_read_frame(dec, &vf);

        /* Read audio */
        int n = blunt_decoder_read_audio_frame(dec, frame_buf, max_per_frame);
        if (n > 0) {
            if (total_written + n > total_alloc) {
                total_alloc = (total_written + n) * 2;
                all_pcm = (int16_t *)realloc(all_pcm, total_alloc * sizeof(int16_t));
            }
            memcpy(all_pcm + total_written, frame_buf, n * sizeof(int16_t));
            total_written += n;
        }

        if (f % 10 == 0)
            printf("\rDecoded %d/%d frames", f + 1, hdr.num_frames);
    }
    printf("\rDecoded %d frames, %d audio samples\n", hdr.num_frames, total_written);

    blunt_frame_free(&vf);
    blunt_decoder_destroy(dec);
    free(frame_buf);

    if (blunt_wav_write(out_wav, all_pcm, total_written / channels,
                        channels, sample_rate, bits) != 0) {
        fprintf(stderr, "Error: cannot write %s\n", out_wav);
        free(all_pcm);
        return 1;
    }

    printf("Wrote %s (%d samples, %d ch, %d Hz)\n",
           out_wav, total_written / channels, channels, sample_rate);

    free(all_pcm);
    return 0;
}

int main(int argc, char **argv) {
    const char *in_path = NULL;
    const char *out_path = NULL;
    int quality = 75;
    int width = 320;
    int height = 240;
    int fps = 30;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-i") == 0 && i + 1 < argc) in_path = argv[++i];
        else if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) out_path = argv[++i];
        else if (strcmp(argv[i], "-q") == 0 && i + 1 < argc) quality = atoi(argv[++i]);
        else if (strcmp(argv[i], "-w") == 0 && i + 1 < argc) width = atoi(argv[++i]);
        else if (strcmp(argv[i], "-h") == 0 && i + 1 < argc) height = atoi(argv[++i]);
        else if (strcmp(argv[i], "-f") == 0 && i + 1 < argc) fps = atoi(argv[++i]);
        else { usage(argv[0]); return 1; }
    }

    if (!in_path || !out_path) { usage(argv[0]); return 1; }

    if (is_blunt(in_path))
        return blunt_to_wav(in_path, out_path);
    else
        return wav_to_blunt(in_path, out_path, quality, width, height, fps);
}
