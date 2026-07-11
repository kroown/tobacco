#include "blunt_audio.h"
#include "blunt_tables.h"
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
    printf("=== Audio Direct Debug ===\n");

    blunt_init_audio_quant(QUALITY);

    int total_samples_per_ch = SAMPLE_RATE * DURATION_S;
    int samples_per_frame = SAMPLE_RATE / 30;

    /* Just 1 frame first */
    printf("=== Single frame test (1470 samples/ch, 2ch) ===\n");

    int16_t *pcm = (int16_t *)malloc(samples_per_frame * CHANNELS * sizeof(int16_t));
    for (int i = 0; i < samples_per_frame; i++) {
        double t = (double)i / SAMPLE_RATE;
        double val = sin(2.0 * 3.14159265358979 * FREQUENCY * t) * 16000.0;
        for (int ch = 0; ch < CHANNELS; ch++)
            pcm[i * CHANNELS + ch] = (int16_t)val;
    }

    int qp = ((100 - QUALITY) * 63) / 100;

    uint8_t buf[65536];
    int sz = blunt_audio_encode_frame(pcm, samples_per_frame, CHANNELS, qp, buf, sizeof(buf));
    printf("Encoded %d bytes for %d samples/ch\n", sz, samples_per_frame);

    int16_t *decoded = (int16_t *)calloc(samples_per_frame * CHANNELS, sizeof(int16_t));
    int n = blunt_audio_decode_frame(buf, sz, CHANNELS, qp, decoded, samples_per_frame * CHANNELS);
    printf("Decoded %d values (out_cap=%d)\n", n, samples_per_frame * CHANNELS);

    double total_err = 0.0;
    int compared = 0;
    for (int i = 0; i < n && i < samples_per_frame * CHANNELS; i++) {
        double diff = (double)pcm[i] - (double)decoded[i];
        total_err += diff * diff;
        compared++;
    }
    double mse = total_err / compared;
    double psnr = (mse > 0) ? 10.0 * log10(32767.0 * 32767.0 / mse) : 999.0;
    printf("Single frame PSNR: %.2f dB\n", psnr);

    /* Show ch1 sample errors */
    int ch1_errors = 0;
    for (int i = 0; i < samples_per_frame; i++) {
        if (pcm[i * CHANNELS + 1] != decoded[i * CHANNELS + 1])
            ch1_errors++;
    }
    printf("Ch1 errors: %d / %d\n", ch1_errors, samples_per_frame);

    /* Show first 20 interleaved samples */
    printf("\nFirst 20 interleaved samples:\n");
    for (int i = 0; i < 20; i++) {
        printf("  [%2d] orig=%6d decoded=%6d diff=%d\n",
               i, pcm[i], decoded[i], pcm[i] - decoded[i]);
    }

    free(pcm);
    free(decoded);
    return 0;
}
