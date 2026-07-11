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
    printf("=== Direct Audio Codec Test (no file I/O) ===\n");

    blunt_init_audio_quant(QUALITY);

    int total_samples_per_ch = SAMPLE_RATE * DURATION_S;
    int samples_per_frame = SAMPLE_RATE / 30;
    int total_frames = total_samples_per_ch / samples_per_frame;

    /* Generate stereo sine wave */
    int16_t *pcm = (int16_t *)malloc(total_samples_per_ch * CHANNELS * sizeof(int16_t));
    for (int i = 0; i < total_samples_per_ch; i++) {
        double t = (double)i / SAMPLE_RATE;
        double val = sin(2.0 * 3.14159265358979 * FREQUENCY * t) * 16000.0;
        for (int ch = 0; ch < CHANNELS; ch++)
            pcm[i * CHANNELS + ch] = (int16_t)val;
    }

    int qp = ((100 - QUALITY) * 63) / 100;

    /* Encode and decode frame by frame */
    int16_t *decoded = (int16_t *)calloc(total_samples_per_ch * CHANNELS, sizeof(int16_t));
    int dec_idx = 0;

    for (int f = 0; f < total_frames; f++) {
        uint8_t buf[65536];
        int samples_this_frame = samples_per_frame;
        if (f == total_frames - 1)
            samples_this_frame = total_samples_per_ch - f * samples_per_frame;

        int sz = blunt_audio_encode_frame(
            pcm + f * samples_per_frame * CHANNELS,
            samples_this_frame, CHANNELS, qp,
            buf, sizeof(buf));

        if (sz < 0) {
            printf("  Frame %d: encode failed\n", f);
            continue;
        }

        int n = blunt_audio_decode_frame(
            buf, sz, CHANNELS, qp,
            decoded + dec_idx,
            samples_this_frame * CHANNELS);

        dec_idx += n;
    }

    printf("  Encoded/decoded %d frames, %d total samples\n", total_frames, dec_idx);

    /* Compare */
    double total_err = 0.0;
    int compared = 0;
    for (int i = 0; i < dec_idx && i < total_samples_per_ch * CHANNELS; i++) {
        double diff = (double)pcm[i] - (double)decoded[i];
        total_err += diff * diff;
        compared++;
    }

    double mse = total_err / compared;
    double psnr = (mse > 0) ? 10.0 * log10(32767.0 * 32767.0 / mse) : 999.0;

    printf("  MSE: %.2f\n", mse);
    printf("  Audio PSNR: %.2f dB\n", psnr);

    /* Show first few samples */
    printf("\n  First 10 samples comparison:\n");
    for (int i = 0; i < 10 && i < compared; i++) {
        printf("    [%d] orig=%6d decoded=%6d diff=%d\n",
               i, pcm[i], decoded[i], pcm[i] - decoded[i]);
    }

    /* Also test a single block roundtrip */
    printf("\n=== Single block test ===\n");
    int16_t block_in[16] = {1000, 2000, -3000, 4000, 500, -1500, 2500, -3500,
                             1234, -5678, 9012, -3456, 7890, -1234, 5678, -9012};
    int32_t coded[16];
    int16_t block_out[16];

    blunt_audio_encode_block(block_in, coded, qp);
    blunt_audio_decode_block(coded, block_out, qp);

    printf("  qp=%d\n", qp);
    int block_ok = 1;
    for (int i = 0; i < 16; i++) {
        if (block_in[i] != block_out[i]) {
            printf("  MISMATCH [%d]: %d -> %d (diff=%d, coded=%d)\n",
                   i, block_in[i], block_out[i], block_in[i] - block_out[i], coded[i]);
            block_ok = 0;
        }
    }
    if (block_ok) printf("  PASS: single block lossless\n");

    /* Test with larger values that would overflow int16 */
    printf("\n=== Large value test (would overflow int16 WHT) ===\n");
    int16_t big_block[16] = {32000, -31000, 30000, -29000, 28000, -27000, 26000, -25000,
                              24000, -23000, 22000, -21000, 20000, -19000, 18000, -17000};
    int32_t big_coded[16];
    int16_t big_out[16];

    blunt_audio_encode_block(big_block, big_coded, qp);
    blunt_audio_decode_block(big_coded, big_out, qp);

    printf("  qp=%d\n", qp);
    for (int i = 0; i < 16; i++) {
        printf("    [%2d] in=%6d coded=%8d out=%6d diff=%d\n",
               i, big_block[i], big_coded[i], big_out[i], big_block[i] - big_out[i]);
    }

    free(pcm);
    free(decoded);
    return (psnr > 20.0) ? 0 : 1;
}
