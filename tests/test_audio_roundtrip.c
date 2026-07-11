#include "blunt.h"
#include "blunt_audio.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define SAMPLE_RATE 44100
#define DURATION_S  2
#define FREQUENCY   440.0
#define CHANNELS    2
#define QUALITY     80

int main(void) {
    printf("=== BLUNT Audio Roundtrip Test ===\n");
    printf("Rate: %d Hz, Ch: %d, Freq: %.0f Hz, Duration: %ds, Q: %d\n\n",
           SAMPLE_RATE, CHANNELS, FREQUENCY, DURATION_S, QUALITY);

    int total_samples = SAMPLE_RATE * DURATION_S;
    int samples_per_frame = SAMPLE_RATE / 30; /* 30 fps */
    int total_frames = total_samples / samples_per_frame;

    printf("[1] Generating %d Hz sine wave (%d samples)...\n",
           (int)FREQUENCY, total_samples);

    int16_t *pcm = (int16_t *)malloc(total_samples * CHANNELS * sizeof(int16_t));
    for (int i = 0; i < total_samples; i++) {
        double t = (double)i / SAMPLE_RATE;
        double val = sin(2.0 * 3.14159265358979 * FREQUENCY * t) * 16000.0;
        for (int ch = 0; ch < CHANNELS; ch++)
            pcm[i * CHANNELS + ch] = (int16_t)val;
    }

    printf("[2] Encoding to test_audio.blunt...\n");

    BluntEncoder *enc = blunt_encoder_create();
    blunt_encoder_set_quality(enc, QUALITY);
    blunt_encoder_set_audio(enc, CHANNELS, SAMPLE_RATE);
    blunt_encoder_open(enc, "test_audio.blunt", 320, 240, 30, 1);

    /* Write blank video frames + audio */
    int mbw = (320 + 15) / 16;
    int mbh = (240 + 15) / 16;
    BluntFrame vframe;
    blunt_frame_alloc(&vframe, mbw, mbh);
    memset(vframe.y, 16, vframe.y_stride * 240);
    memset(vframe.cb, 128, vframe.cb_stride * 120);
    memset(vframe.cr, 128, vframe.cr_stride * 120);

    int sample_idx = 0;
    for (int f = 0; f < total_frames; f++) {
        blunt_encoder_write_frame(enc, &vframe, f == 0);
        blunt_encoder_write_audio_frame(enc, pcm + sample_idx, samples_per_frame);
        sample_idx += samples_per_frame * CHANNELS;
    }
    blunt_frame_free(&vframe);
    blunt_encoder_close(enc);
    blunt_encoder_destroy(enc);
    printf("  Encoded %d frames\n", total_frames);

    printf("[3] Decoding from test_audio.blunt...\n");

    BluntDecoder *dec = blunt_decoder_create();
    blunt_decoder_open(dec, "test_audio.blunt");

    BluntHeader hdr;
    blunt_decoder_read_header(dec, &hdr);
    printf("  Header: %dx%d, %d frames, audio=%d Hz %d-ch %d-bit\n",
           hdr.width, hdr.height, hdr.num_frames,
           hdr.audio_sample_rate, hdr.audio_channels, hdr.audio_bits);

    int16_t *decoded = (int16_t *)calloc(total_samples * CHANNELS, sizeof(int16_t));
    int dec_idx = 0;

    for (uint32_t f = 0; f < hdr.num_frames; f++) {
        BluntFrame vf;
        blunt_frame_alloc(&vf, hdr.mb_width, hdr.mb_height);
        blunt_decoder_read_frame(dec, &vf);

        int16_t frame_audio[samples_per_frame * CHANNELS * 2];
        int n = blunt_decoder_read_audio_frame(dec, frame_audio,
                                               samples_per_frame * CHANNELS);
        if (n > 0 && dec_idx + n <= total_samples * CHANNELS) {
            memcpy(decoded + dec_idx, frame_audio, n * sizeof(int16_t));
            dec_idx += n;
        }
        blunt_frame_free(&vf);
    }
    blunt_decoder_destroy(dec);
    printf("  Decoded %d audio samples\n", dec_idx);

    printf("[4] Comparing...\n");

    double total_err = 0.0;
    int compared = 0;
    for (int i = 0; i < dec_idx && i < total_samples * CHANNELS; i++) {
        double diff = (double)pcm[i] - (double)decoded[i];
        total_err += diff * diff;
        compared++;
    }

    double mse = total_err / compared;
    double psnr = (mse > 0) ? 10.0 * log10(32767.0 * 32767.0 / mse) : 999.0;

    printf("  MSE: %.2f\n", mse);
    printf("  Audio PSNR: %.2f dB\n", psnr);

    int ok = (psnr > 20.0);
    printf("\n[5] Result: %s (%.2f dB %s 20 dB)\n",
           ok ? "PASS" : "FAIL", psnr, ok ? ">" : "<");

    free(pcm);
    free(decoded);
    return ok ? 0 : 1;
}
