#ifndef BLUNT_H
#define BLUNT_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BLUNT_MAGIC          "BLNT"
#define BLUNT_MAGIC_UINT32   0x544E4C42
#define BLUNT_VERSION        1
#define BLUNT_HEADER_SIZE    64
#define BLUNT_FRAME_HDR_SIZE 17
#define BLUNT_MAX_WIDTH      16384
#define BLUNT_MAX_HEIGHT     16384

#define BLUNT_MB_SIZE        16
#define BLUNT_BLOCK_SIZE     4
#define BLUNT_BLOCK_AREA     16

#define BLUNT_FLAG_ALPHA     (1 << 0)
#define BLUNT_FLAG_INTERLACE (1 << 1)
#define BLUNT_FLAG_HAS_AUDIO (1 << 2)

#define BLUNT_FRAME_I        0
#define BLUNT_FRAME_P        1

#define BLUNT_MAX_FRAMES     1000000

#pragma pack(push, 1)

typedef struct {
    char     magic[4];
    uint16_t version;
    uint16_t header_size;
    uint16_t width;
    uint16_t height;
    uint16_t fps_num;
    uint16_t fps_den;
    uint32_t num_frames;
    uint16_t mb_width;
    uint16_t mb_height;
    uint8_t  flags;
    uint8_t  quality;
    uint16_t num_keyframes;
    uint16_t audio_sample_rate;
    uint8_t  audio_channels;
    uint8_t  audio_bits;
    uint8_t  reserved[24];
} BluntHeader;

typedef struct {
    uint32_t frame_num;
    uint8_t  frame_type;
    uint32_t data_size;
    uint32_t timestamp_ms;
    uint16_t ref_frame;
    uint16_t audio_data_size;
} BluntFrameHeader;

#pragma pack(pop)

typedef struct {
    uint8_t *y, *cb, *cr;
    int      y_stride, cb_stride, cr_stride;
} BluntFrame;

typedef struct BluntDecoder BluntDecoder;
typedef struct BluntEncoder BluntEncoder;

typedef void (*BluntProgressFn)(int current, int total, void *userdata);

BluntDecoder *blunt_decoder_create(void);
void          blunt_decoder_destroy(BluntDecoder *dec);

int  blunt_decoder_open(BluntDecoder *dec, const char *path);
int  blunt_decoder_read_header(BluntDecoder *dec, BluntHeader *hdr);
int  blunt_decoder_read_frame(BluntDecoder *dec, BluntFrame *out);
int  blunt_decoder_read_audio_frame(BluntDecoder *dec, int16_t *out,
                                    int max_samples);
void blunt_decoder_seek(BluntDecoder *dec, uint32_t frame_num);
void blunt_decoder_set_progress(BluntDecoder *dec, BluntProgressFn fn, void *ud);
int  blunt_decoder_decode_frame(BluntDecoder *dec, uint32_t frame_num, BluntFrame *out);

BluntEncoder *blunt_encoder_create(void);
void          blunt_encoder_destroy(BluntEncoder *enc);

int  blunt_encoder_set_quality(BluntEncoder *enc, uint8_t quality);
int  blunt_encoder_set_audio(BluntEncoder *enc, int channels, int sample_rate);
int  blunt_encoder_open(BluntEncoder *enc, const char *path,
                        uint16_t width, uint16_t height,
                        uint16_t fps_num, uint16_t fps_den);
int  blunt_encoder_write_frame(BluntEncoder *enc, const BluntFrame *frame,
                               int force_keyframe);
int  blunt_encoder_write_audio_frame(BluntEncoder *enc,
                                     const int16_t *samples, int num_samples);
int  blunt_encoder_close(BluntEncoder *enc);

int blunt_frame_alloc(BluntFrame *f, int mb_width, int mb_height);
void blunt_frame_free(BluntFrame *f);

int blunt_rgb_to_ycbcr(const uint8_t *rgb, int w, int h, int stride,
                        BluntFrame *out);
int blunt_ycbcr_to_rgb(const BluntFrame *f, uint8_t *rgb, int w, int h,
                       int stride);

const char *blunt_version_string(void);

#ifdef __cplusplus
}
#endif

#endif
