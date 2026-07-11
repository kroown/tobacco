#ifndef BLUNT_AUDIO_H
#define BLUNT_AUDIO_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BLUNT_AUDIO_BLOCK_SAMPLES 16
#define BLUNT_AUDIO_MAX_CHANNELS  2

typedef struct {
    int16_t *samples;
    int      num_samples;
    int      channels;
} BluntAudioFrame;

extern int32_t blunt_audio_quant[64][BLUNT_AUDIO_BLOCK_SAMPLES];

void blunt_init_audio_quant(int quality);

void blunt_audio_encode_block(const int16_t *input, int32_t *output,
                              int qp);
void blunt_audio_decode_block(const int32_t *input, int16_t *output,
                              int qp);

int  blunt_audio_encode_frame(const int16_t *samples, int num_samples,
                              int channels, int qp,
                              uint8_t *out, int out_cap);
int  blunt_audio_decode_frame(const uint8_t *data, int data_size,
                              int channels, int qp,
                              int16_t *out, int out_cap);

int  blunt_wav_read_header(const char *path, int *channels,
                           int *sample_rate, int *bits,
                           int *data_offset, int *data_size);
int  blunt_wav_read_samples(const char *path, int data_offset,
                            int16_t *out, int max_samples);

int  blunt_wav_write(const char *path, const int16_t *samples,
                     int num_samples, int channels,
                     int sample_rate, int bits);

#ifdef __cplusplus
}
#endif

#endif
