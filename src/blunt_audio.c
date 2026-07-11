#include "blunt_audio.h"
#include "blunt_tables.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int32_t blunt_audio_quant[64][BLUNT_AUDIO_BLOCK_SAMPLES];

static const int audio_default_qmatrix[16] = {
     8,  8,  9, 10,
    10, 12, 14, 16,
    12, 14, 18, 22,
    16, 20, 26, 32
};

void blunt_init_audio_quant(int quality) {
    if (quality < 1) quality = 1;
    if (quality > 100) quality = 100;
    int scale = (quality < 50) ? (5000 / quality) : (200 - 2 * quality);

    for (int qp = 0; qp < 64; qp++) {
        int qp_adj = (qp >> 1) + (qp & 1);
        for (int i = 0; i < 16; i++) {
            int q = (audio_default_qmatrix[i] * scale + 50) / 100;
            q = (q * qp_adj + 2) >> 2;
            if (q < 1) q = 1;
            if (q > 4095) q = 4095;
            blunt_audio_quant[qp][blunt_zigzag_scan[i]] = q;
        }
    }
}

void blunt_audio_encode_block(const int16_t *input, int32_t *output,
                              int qp) {
    if (qp < 0) qp = 0;
    if (qp > 63) qp = 63;

    int32_t coeff[16];
    int32_t tmp[16];
    for (int i = 0; i < 4; i++) {
        int32_t s0 = input[i*4+0], s1 = input[i*4+1];
        int32_t s2 = input[i*4+2], s3 = input[i*4+3];
        int32_t a = s0 + s1, c = s2 + s3;
        int32_t b = s0 - s1, d = s2 - s3;
        tmp[i*4+0] = a + c;
        tmp[i*4+1] = a - c;
        tmp[i*4+2] = b - d;
        tmp[i*4+3] = b + d;
    }
    for (int i = 0; i < 4; i++) {
        int32_t s0 = tmp[0*4+i], s1 = tmp[1*4+i];
        int32_t s2 = tmp[2*4+i], s3 = tmp[3*4+i];
        int32_t a = s0 + s1, c = s2 + s3;
        int32_t b = s0 - s1, d = s2 - s3;
        coeff[0*4+i] = a + c;
        coeff[1*4+i] = a - c;
        coeff[2*4+i] = b - d;
        coeff[3*4+i] = b + d;
    }

    for (int i = 0; i < 16; i++) {
        int q = (int)blunt_audio_quant[qp][i];
        if (q == 0) q = 1;
        output[i] = (coeff[i] + (q >> 1)) / q;
    }
}

void blunt_audio_decode_block(const int32_t *input, int16_t *output,
                              int qp) {
    if (qp < 0) qp = 0;
    if (qp > 63) qp = 63;

    int32_t coeff[16];
    for (int i = 0; i < 16; i++) {
        int q = (int)blunt_audio_quant[qp][i];
        coeff[i] = input[i] * q;
    }

    int32_t tmp[16];
    for (int i = 0; i < 4; i++) {
        int32_t s0 = coeff[0*4+i], s1 = coeff[1*4+i];
        int32_t s2 = coeff[2*4+i], s3 = coeff[3*4+i];
        int32_t a = s0 + s1, c = s2 + s3;
        int32_t b = s0 - s1, d = s2 - s3;
        tmp[0*4+i] = (a + c + 2) >> 2;
        tmp[1*4+i] = (a - c + 2) >> 2;
        tmp[2*4+i] = (b - d + 2) >> 2;
        tmp[3*4+i] = (b + d + 2) >> 2;
    }
    for (int i = 0; i < 4; i++) {
        int32_t s0 = tmp[i*4+0], s1 = tmp[i*4+1];
        int32_t s2 = tmp[i*4+2], s3 = tmp[i*4+3];
        int32_t a = s0 + s1, c = s2 + s3;
        int32_t b = s0 - s1, d = s2 - s3;
        int32_t v;
        v = (a + c + 2) >> 2;
        output[i*4+0] = (int16_t)(v < -32768 ? -32768 : (v > 32767 ? 32767 : v));
        v = (a - c + 2) >> 2;
        output[i*4+1] = (int16_t)(v < -32768 ? -32768 : (v > 32767 ? 32767 : v));
        v = (b - d + 2) >> 2;
        output[i*4+2] = (int16_t)(v < -32768 ? -32768 : (v > 32767 ? 32767 : v));
        v = (b + d + 2) >> 2;
        output[i*4+3] = (int16_t)(v < -32768 ? -32768 : (v > 32767 ? 32767 : v));
    }
}

typedef struct {
    uint8_t *data;
    size_t   capacity;
    size_t   byte_pos;
    int      bit_pos;
} ABW;

static void abw_init(ABW *w, size_t cap) {
    w->data = (uint8_t *)calloc(1, cap);
    w->capacity = cap;
    w->byte_pos = 0;
    w->bit_pos = 0;
}

static void abw_write(ABW *w, uint32_t val, int nbits) {
    for (int i = nbits - 1; i >= 0; i--) {
        if (w->byte_pos >= w->capacity) {
            w->capacity *= 2;
            w->data = (uint8_t *)realloc(w->data, w->capacity);
        }
        w->data[w->byte_pos] |= ((val >> i) & 1) << (7 - w->bit_pos);
        w->bit_pos++;
        if (w->bit_pos >= 8) {
            w->bit_pos = 0;
            w->byte_pos++;
        }
    }
}

static size_t abw_flush(ABW *w) {
    if (w->bit_pos > 0) w->byte_pos++;
    return w->byte_pos;
}

static void encode_audio_runlevel(ABW *bw, const int32_t *zigzag) {
    int idx = 0, run = 0;
    while (idx < 16) {
        if (zigzag[idx] == 0) { run++; idx++; continue; }
        int32_t level = zigzag[idx];
        int32_t abs_level = level < 0 ? -level : level;
        int sign = level < 0 ? 1 : 0;
        if (abs_level <= 13) {
            abw_write(bw, (run << 4) | (uint32_t)abs_level, 8);
            abw_write(bw, (uint32_t)sign, 1);
        } else if (abs_level <= 32767) {
            abw_write(bw, (run << 4) | 0x0E, 8);
            abw_write(bw, (uint32_t)(int16_t)level, 16);
        } else {
            abw_write(bw, (run << 4) | 0x0F, 8);
            abw_write(bw, (uint32_t)level, 32);
        }
        run = 0;
        idx++;
    }
    abw_write(bw, 0x00, 8);
}

int blunt_audio_encode_frame(const int16_t *samples, int num_samples,
                             int channels, int qp,
                             uint8_t *out, int out_cap) {
    int samples_per_block = BLUNT_AUDIO_BLOCK_SAMPLES;
    int blocks = (num_samples + samples_per_block - 1) / samples_per_block;

    ABW bw;
    abw_init(&bw, num_samples * channels * 4);

    int16_t block[16];

    for (int ch = 0; ch < channels; ch++) {
        for (int b = 0; b < blocks; b++) {
            memset(block, 0, sizeof(block));
            for (int i = 0; i < samples_per_block; i++) {
                int idx = b * samples_per_block + i;
                if (idx < num_samples)
                    block[i] = samples[idx * channels + ch];
            }
            int32_t coded[16];
            int32_t zigzag[16];
            blunt_audio_encode_block(block, coded, qp);
            for (int i = 0; i < 16; i++)
                zigzag[i] = coded[blunt_zigzag_scan[i]];
            encode_audio_runlevel(&bw, zigzag);
        }
    }

    size_t sz = abw_flush(&bw);
    if ((int)sz > out_cap) { free(bw.data); return -1; }
    memcpy(out, bw.data, sz);
    free(bw.data);
    return (int)sz;
}

typedef struct {
    const uint8_t *data;
    size_t         len;
    size_t         byte_pos;
    int            bit_pos;
} ABR;

static void abr_init(ABR *r, const uint8_t *data, size_t len) {
    r->data = data;
    r->len = len;
    r->byte_pos = 0;
    r->bit_pos = 0;
}

static uint32_t abr_read(ABR *r, int nbits) {
    uint32_t val = 0;
    for (int i = 0; i < nbits; i++) {
        if (r->byte_pos >= r->len) return val;
        val <<= 1;
        val |= (r->data[r->byte_pos] >> (7 - r->bit_pos)) & 1;
        r->bit_pos++;
        if (r->bit_pos >= 8) { r->bit_pos = 0; r->byte_pos++; }
    }
    return val;
}

static int decode_audio_runlevel(ABR *br, int32_t *block) {
    int idx = 0;
    int hit_eob = 0;
    while (idx < 16) {
        uint32_t rl = abr_read(br, 8);
        int run = (rl >> 4) & 0xF;
        int level = rl & 0xF;
        if (level == 0 && run == 0) { hit_eob = 1; break; }
        if (level == 0xF) {
            int32_t raw = (int32_t)abr_read(br, 32);
            idx += run;
            if (idx < 16) block[idx] = raw;
            idx++;
        } else if (level == 0xE) {
            int32_t raw = (int32_t)(int16_t)(uint16_t)abr_read(br, 16);
            idx += run;
            if (idx < 16) block[idx] = raw;
            idx++;
        } else {
            int sign = abr_read(br, 1);
            int val = sign ? -level : level;
            idx += run;
            if (idx < 16) block[idx] = val;
            idx++;
        }
    }
    if (!hit_eob && idx == 16)
        abr_read(br, 8);
    return 0;
}

int blunt_audio_decode_frame(const uint8_t *data, int data_size,
                             int channels, int qp,
                             int16_t *out, int out_cap) {
    ABR br;
    abr_init(&br, data, data_size);

    int written = 0;

    int ch_blocks = (out_cap / channels + BLUNT_AUDIO_BLOCK_SAMPLES - 1)
                    / BLUNT_AUDIO_BLOCK_SAMPLES;

    for (int ch = 0; ch < channels; ch++) {
        for (int b = 0; b < ch_blocks; b++) {
            int32_t zigzag[16] = {0};
            int32_t coded[16] = {0};
            int16_t decoded[16] = {0};
            decode_audio_runlevel(&br, zigzag);
            for (int i = 0; i < 16; i++)
                coded[blunt_zigzag_scan[i]] = zigzag[i];
            blunt_audio_decode_block(coded, decoded, qp);
            for (int i = 0; i < BLUNT_AUDIO_BLOCK_SAMPLES; i++) {
                int sample_idx = b * BLUNT_AUDIO_BLOCK_SAMPLES + i;
                int out_idx = sample_idx * channels + ch;
                if (sample_idx < (out_cap / channels) && out_idx < out_cap) {
                    out[out_idx] = decoded[i];
                    if (out_idx + 1 > written)
                        written = out_idx + 1;
                }
            }
        }
    }
    return written;
}

int blunt_wav_read_header(const char *path, int *channels,
                          int *sample_rate, int *bits,
                          int *data_offset, int *data_size) {
    FILE *fp = fopen(path, "rb");
    if (!fp) return -1;

    uint8_t hdr[44];
    if (fread(hdr, 1, 44, fp) != 44) { fclose(fp); return -1; }
    if (memcmp(hdr, "RIFF", 4) != 0 || memcmp(hdr + 8, "WAVE", 4) != 0) {
        fclose(fp); return -1;
    }

    *channels = *(uint16_t *)(hdr + 22);
    *sample_rate = *(uint32_t *)(hdr + 24);
    *bits = *(uint16_t *)(hdr + 34);

    uint32_t chunk_size = *(uint32_t *)(hdr + 40);
    *data_offset = 44;
    *data_size = (int)chunk_size;

    while (1) {
        uint8_t tag[4];
        uint32_t sz;
        if (fread(tag, 1, 4, fp) != 4) break;
        if (fread(&sz, 4, 1, fp) != 1) break;
        if (memcmp(tag, "data", 4) == 0) {
            *data_offset = (int)ftell(fp);
            *data_size = (int)sz;
            break;
        }
        fseek(fp, sz, SEEK_CUR);
    }

    fclose(fp);
    return 0;
}

int blunt_wav_read_samples(const char *path, int data_offset,
                           int16_t *out, int max_samples) {
    FILE *fp = fopen(path, "rb");
    if (!fp) return -1;
    fseek(fp, data_offset, SEEK_SET);
    int n = (int)fread(out, sizeof(int16_t), max_samples, fp);
    fclose(fp);
    return n;
}

int blunt_wav_write(const char *path, const int16_t *samples,
                    int num_samples, int channels,
                    int sample_rate, int bits) {
    FILE *fp = fopen(path, "wb");
    if (!fp) return -1;

    int byte_rate = sample_rate * channels * bits / 8;
    int block_align = channels * bits / 8;
    int data_bytes = num_samples * channels * (bits / 8);

    fwrite("RIFF", 1, 4, fp);
    uint32_t riff_size = 36 + data_bytes;
    fwrite(&riff_size, 4, 1, fp);
    fwrite("WAVE", 1, 4, fp);

    fwrite("fmt ", 1, 4, fp);
    uint32_t fmt_size = 16;
    fwrite(&fmt_size, 4, 1, fp);
    uint16_t fmt = 1;
    fwrite(&fmt, 2, 1, fp);
    uint16_t ch = (uint16_t)channels;
    fwrite(&ch, 2, 1, fp);
    uint32_t sr = (uint32_t)sample_rate;
    fwrite(&sr, 4, 1, fp);
    uint32_t br = (uint32_t)byte_rate;
    fwrite(&br, 4, 1, fp);
    uint16_t ba = (uint16_t)block_align;
    fwrite(&ba, 2, 1, fp);
    uint16_t bps = (uint16_t)bits;
    fwrite(&bps, 2, 1, fp);

    fwrite("data", 1, 4, fp);
    uint32_t dsize = (uint32_t)data_bytes;
    fwrite(&dsize, 4, 1, fp);
    fwrite(samples, sizeof(int16_t), num_samples, fp);

    fclose(fp);
    return 0;
}
