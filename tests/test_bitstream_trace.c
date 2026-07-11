#include "blunt_audio.h"
#include "blunt_tables.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

// patched encoder that tracks byte positions per block
typedef struct {
    uint8_t *data;
    size_t   capacity;
    size_t   byte_pos;
    int      bit_pos;
} TBW;

static void tbw_init(TBW *w, size_t cap) {
    w->data = (uint8_t *)calloc(1, cap);
    w->capacity = cap;
    w->byte_pos = 0;
    w->bit_pos = 0;
}

static void tbw_write(TBW *w, uint32_t val, int nbits) {
    for (int i = nbits - 1; i >= 0; i--) {
        if (w->byte_pos >= w->capacity) {
            w->capacity *= 2;
            w->data = (uint8_t *)realloc(w->data, w->capacity);
        }
        w->data[w->byte_pos] |= ((val >> i) & 1) << (7 - w->bit_pos);
        w->bit_pos++;
        if (w->bit_pos >= 8) { w->bit_pos = 0; w->byte_pos++; }
    }
}

static size_t tbw_flush(TBW *w) {
    if (w->bit_pos > 0) w->byte_pos++;
    return w->byte_pos;
}

// patched decoder that tracks byte positions
typedef struct {
    const uint8_t *data;
    size_t         len;
    size_t         byte_pos;
    int            bit_pos;
} TBR;

static void tbr_init(TBR *r, const uint8_t *data, size_t len) {
    r->data = data;
    r->len = len;
    r->byte_pos = 0;
    r->bit_pos = 0;
}

static uint32_t tbr_read(TBR *r, int nbits) {
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

static size_t tbr_pos(TBR *r) {
    return r->byte_pos * 8 + r->bit_pos;
}

static void encode_runlevel(TBW *bw, const int32_t *zigzag) {
    int idx = 0, run = 0;
    while (idx < 16) {
        if (zigzag[idx] == 0) { run++; idx++; continue; }
        int32_t level = zigzag[idx];
        int32_t abs_level = level < 0 ? -level : level;
        int sign = level < 0 ? 1 : 0;
        if (abs_level <= 14) {
            tbw_write(bw, (run << 4) | (uint32_t)abs_level, 8);
            tbw_write(bw, (uint32_t)sign, 1);
        } else if (abs_level <= 32767) {
            tbw_write(bw, (run << 4) | 0x0E, 8);
            tbw_write(bw, (uint32_t)(int16_t)level, 16);
        } else {
            tbw_write(bw, (run << 4) | 0x0F, 8);
            tbw_write(bw, (uint32_t)level, 32);
        }
        run = 0;
        idx++;
    }
    tbw_write(bw, 0x00, 8);
}

static int decode_runlevel(TBR *br, int32_t *block) {
    int idx = 0, hit_eob = 0;
    while (idx < 16) {
        uint32_t rl = tbr_read(br, 8);
        int run = (rl >> 4) & 0xF;
        int level = rl & 0xF;
        if (level == 0 && run == 0) { hit_eob = 1; break; }
        if (level == 0xF) {
            int32_t raw = (int32_t)tbr_read(br, 32);
            idx += run;
            if (idx < 16) block[idx] = raw;
            idx++;
        } else if (level == 0xE) {
            int32_t raw = (int32_t)(int16_t)(uint16_t)tbr_read(br, 16);
            idx += run;
            if (idx < 16) block[idx] = raw;
            idx++;
        } else {
            int sign = tbr_read(br, 1);
            int val = sign ? -level : level;
            idx += run;
            if (idx < 16) block[idx] = val;
            idx++;
        }
    }
    if (!hit_eob && idx == 16) tbr_read(br, 8);
    return 0;
}

int main(void) {
    blunt_init_audio_quant(80);
    int qp = ((100 - 80) * 63) / 100;

    int nsamp = 32;
    int16_t in[64];
    for (int i = 0; i < nsamp; i++) {
        double t = (double)i / 44100.0;
        double val = sin(2.0 * 3.14159265358979 * 440.0 * t) * 16000.0;
        in[i*2+0] = (int16_t)val;
        in[i*2+1] = (int16_t)(val * 0.7);
    }

    // encode with position tracking
    TBW bw;
    tbw_init(&bw, 4096);
    int16_t block[16];
    int32_t coded[16], zigzag[16];

    for (int ch = 0; ch < 2; ch++) {
        for (int b = 0; b < 2; b++) {
            memset(block, 0, sizeof(block));
            for (int i = 0; i < 16; i++) {
                int idx = b * 16 + i;
                if (idx < nsamp) block[i] = in[idx * 2 + ch];
            }
            blunt_audio_encode_block(block, coded, qp);
            for (int i = 0; i < 16; i++)
                zigzag[i] = coded[blunt_zigzag_scan[i]];

            size_t pos_before = bw.byte_pos * 8 + bw.bit_pos;
            encode_runlevel(&bw, zigzag);
            size_t pos_after = bw.byte_pos * 8 + bw.bit_pos;
            printf("ENC ch%d b%d: bits %zu -> %zu (%zu bits)\n", ch, b, pos_before, pos_after, pos_after - pos_before);
        }
    }
    size_t total_bits = tbw_flush(&bw);

    // decode with position tracking
    TBR br;
    tbr_init(&br, bw.data, total_bits);

    for (int ch = 0; ch < 2; ch++) {
        for (int b = 0; b < 2; b++) {
            int32_t dec_zigzag[16] = {0};
            int32_t dec_coded[16] = {0};
            int16_t decoded[16] = {0};
            size_t pos_before = tbr_pos(&br);
            decode_runlevel(&br, dec_zigzag);
            size_t pos_after = tbr_pos(&br);
            for (int i = 0; i < 16; i++)
                dec_coded[blunt_zigzag_scan[i]] = dec_zigzag[i];
            blunt_audio_decode_block(dec_coded, decoded, qp);

            int match = 1;
            for (int i = 0; i < 16; i++) {
                int idx = b * 16 + i;
                if (idx < nsamp && decoded[i] != in[idx * 2 + ch]) match = 0;
            }
            printf("DEC ch%d b%d: bits %zu -> %zu (%zu bits) %s\n", ch, b, pos_before, pos_after, pos_after - pos_before, match ? "OK" : "MISMATCH");
        }
    }

    printf("\ntotal bitstream: %zu bits (%zu bytes)\n", total_bits, (total_bits+7)/8);
    free(bw.data);
    return 0;
}
