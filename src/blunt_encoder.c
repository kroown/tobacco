#include "blunt.h"
#include "blunt_tables.h"
#include "blunt_simd.h"
#include "blunt_audio.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

// bitstream writer
typedef struct {
    uint8_t *data;
    size_t   capacity;
    size_t   byte_pos;
    int      bit_pos;
} BitWriter;

static void bw_init(BitWriter *bw, size_t capacity) {
    bw->data = (uint8_t *)calloc(1, capacity);
    bw->capacity = capacity;
    bw->byte_pos = 0;
    bw->bit_pos = 0;
}

static void bw_write(BitWriter *bw, uint32_t val, int nbits) {
    for (int i = nbits - 1; i >= 0; i--) {
        if (bw->byte_pos >= bw->capacity) {
            bw->capacity *= 2;
            bw->data = (uint8_t *)realloc(bw->data, bw->capacity);
        }
        bw->data[bw->byte_pos] |= ((val >> i) & 1) << (7 - bw->bit_pos);
        bw->bit_pos++;
        if (bw->bit_pos >= 8) {
            bw->bit_pos = 0;
            bw->byte_pos++;
        }
    }
}

static size_t bw_flush(BitWriter *bw) {
    if (bw->bit_pos > 0)
        bw->byte_pos++;
    return bw->byte_pos;
}

// run-level encoding
static void encode_block_runlevel(BitWriter *bw, const int16_t *zigzag_block) {
    int idx = 0;
    int run = 0;
    while (idx < 16) {
        if (zigzag_block[idx] == 0) {
            run++;
            idx++;
            continue;
        }

        int level = zigzag_block[idx];
        int abs_level = level < 0 ? -level : level;
        int sign = level < 0 ? 1 : 0;

        if (abs_level <= 14) {
            bw_write(bw, (run << 4) | abs_level, 8);
            bw_write(bw, sign, 1);
        } else {
            /* Escape code */
            bw_write(bw, (run << 4) | 0x0F, 8);
            int16_t raw = level;
            uint32_t raw_enc = (uint32_t)raw & 0xFFFF;
            bw_write(bw, raw_enc, 16);
        }
        run = 0;
        idx++;
    }
    /* End of block */
    bw_write(bw, 0x00, 8);
}

// motion estimation: full search, half-pel, sad-based
static int compute_sad(const uint8_t *a, int as, const uint8_t *b, int bs,
                       int w, int h) {
    int sad = 0;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++)
            sad += abs((int)a[y * as + x] - (int)b[y * bs + x]);
    }
    return sad;
}

static void motion_estimate_16x16(const uint8_t *cur, int cs,
                                   const uint8_t *ref, int rs,
                                   int width, int height,
                                   int *out_dx, int *out_dy) {
    int best_sad = INT32_MAX;
    int best_dx = 0, best_dy = 0;

    int search_range = 16;

    for (int dy = -search_range; dy <= search_range; dy += 2) {
        for (int dx = -search_range; dx <= search_range; dx += 2) {
            int sx = dx, sy = dy;
            if (sx < 0) sx = 0;
            if (sy < 0) sy = 0;
            if (sx + 16 > width) sx = width - 16;
            if (sy + 16 > height) sy = height - 16;

            int sad = compute_sad(cur, cs, ref + sy * rs + sx, rs, 16, 16);
            if (sad < best_sad) {
                best_sad = sad;
                best_dx = dx;
                best_dy = dy;
            }
        }
    }

    /* Half-pel refinement */
    for (int ddy = best_dy - 2; ddy <= best_dy + 2; ddy++) {
        for (int ddx = best_dx - 2; ddx <= best_dx + 2; ddx++) {
            if (ddx < 0 || ddy < 0 || ddx + 16 > width || ddy + 16 > height)
                continue;
            int sad = compute_sad(cur, cs, ref + ddy * rs + ddx, rs, 16, 16);
            if (sad < best_sad) {
                best_sad = sad;
                best_dx = ddx;
                best_dy = ddy;
            }
        }
    }

    *out_dx = best_dx;
    *out_dy = best_dy;
}

// frame encoding
static void encode_block_raw(BitWriter *bw, const int16_t *block,
                             const int16_t *qtable) {
    int16_t coeff[16], zigzag[16];
    blunt_dct4x4_block(block, coeff);
    for (int i = 0; i < 16; i++) {
        int q = qtable[i];
        if (q == 0) q = 1;
        coeff[i] = (int16_t)((coeff[i] + (q >> 1)) / q);
    }
    for (int i = 0; i < 16; i++)
        zigzag[i] = coeff[blunt_zigzag_scan[i]];
    encode_block_runlevel(bw, zigzag);
}

static int encode_frame_i(BitWriter *bw, const BluntFrame *frame,
                          int mbw, int mbh, int qp) {
    for (int mb_y = 0; mb_y < mbh; mb_y++) {
        for (int mb_x = 0; mb_x < mbw; mb_x++) {
            int16_t block[16];

            /* Luma: 16 sub-blocks of 4x4 */
            for (int by = 0; by < 4; by++) {
                for (int bx = 0; bx < 4; bx++) {
                    int px = mb_x * 16 + bx * 4;
                    int py = mb_y * 16 + by * 4;

                    for (int y = 0; y < 4; y++)
                        for (int x = 0; x < 4; x++)
                            block[y * 4 + x] = frame->y[(py + y) * frame->y_stride + (px + x)];

                    encode_block_raw(bw, block, blunt_luma_quant[qp]);
                }
            }

            /* Chroma Cb: 4 sub-blocks of 4x4 (2x2 in chroma space) */
            for (int cby = 0; cby < 2; cby++) {
                for (int cbx = 0; cbx < 2; cbx++) {
                    int cpx = mb_x * 8 + cbx * 4;
                    int cpy = mb_y * 8 + cby * 4;
                    for (int y = 0; y < 4; y++)
                        for (int x = 0; x < 4; x++)
                            block[y * 4 + x] = frame->cb[(cpy + y) * frame->cb_stride + (cpx + x)];
                    encode_block_raw(bw, block, blunt_chroma_quant[qp]);
                }
            }

            /* Chroma Cr: 4 sub-blocks of 4x4 */
            for (int cby = 0; cby < 2; cby++) {
                for (int cbx = 0; cbx < 2; cbx++) {
                    int cpx = mb_x * 8 + cbx * 4;
                    int cpy = mb_y * 8 + cby * 4;
                    for (int y = 0; y < 4; y++)
                        for (int x = 0; x < 4; x++)
                            block[y * 4 + x] = frame->cr[(cpy + y) * frame->cr_stride + (cpx + x)];
                    encode_block_raw(bw, block, blunt_chroma_quant[qp]);
                }
            }
        }
    }
    return 0;
}

static int encode_frame_p(BitWriter *bw, const BluntFrame *cur,
                          const BluntFrame *ref,
                          int mbw, int mbh, int w, int h, int qp) {
    for (int mb_y = 0; mb_y < mbh; mb_y++) {
        for (int mb_x = 0; mb_x < mbw; mb_x++) {
            int16_t block[16], coeff[16], zigzag[16];
            uint8_t pred_block[256];

            /* Luma: 16 sub-blocks of 4x4 with per-block MV */
            for (int by = 0; by < 4; by++) {
                for (int bx = 0; bx < 4; bx++) {
                    int px = mb_x * 16 + bx * 4;
                    int py = mb_y * 16 + by * 4;

                    /* Motion estimate against reference */
                    int mvx = 0, mvy = 0;
                    motion_estimate_16x16(
                        cur->y + py * cur->y_stride + px, cur->y_stride,
                        ref->y + py * ref->y_stride + px, ref->y_stride,
                        w - px, h - py,
                        &mvx, &mvy);

                    /* Compensate (integer pixel) */
                    int sx = px + mvx, sy = py + mvy;
                    if (sx < 0) sx = 0;
                    if (sy < 0) sy = 0;
                    if (sx + 4 > w) sx = w - 4;
                    if (sy + 4 > h) sy = h - 4;

                    for (int y = 0; y < 4; y++)
                        for (int x = 0; x < 4; x++)
                            pred_block[y * 4 + x] = ref->y[(sy + y) * ref->y_stride + (sx + x)];

                    /* Residual = original - prediction */
                    for (int y = 0; y < 4; y++)
                        for (int x = 0; x < 4; x++)
                            block[y * 4 + x] = cur->y[(py + y) * cur->y_stride + (px + x)] - pred_block[y * 4 + x];

                    blunt_dct4x4_block(block, coeff);
                    for (int i = 0; i < 16; i++) {
                        int q = blunt_luma_quant[qp][i];
                        if (q == 0) q = 1;
                        coeff[i] = (int16_t)((coeff[i] + (q >> 1)) / q);
                    }
                    for (int i = 0; i < 16; i++)
                        zigzag[i] = coeff[blunt_zigzag_scan[i]];

                    /* Write MV */
                    uint32_t mv_enc = ((uint32_t)(mvy & 0xFF) << 8) | (uint32_t)(mvx & 0xFF);
                    bw_write(bw, mv_enc, 16);
                    encode_block_runlevel(bw, zigzag);
                }
            }

            /* Chroma: 4 sub-blocks of 4x4 (2x2 in chroma space) */
            for (int cby = 0; cby < 2; cby++) {
                for (int cbx = 0; cbx < 2; cbx++) {
                    int cpx = mb_x * 8 + cbx * 4;
                    int cpy = mb_y * 8 + cby * 4;

                    int cmvx = 0, cmvy = 0;
                    motion_estimate_16x16(
                        cur->cb + cpy * cur->cb_stride + cpx, cur->cb_stride,
                        ref->cb + cpy * ref->cb_stride + cpx, ref->cb_stride,
                        (w/2) - cpx, (h/2) - cpy,
                        &cmvx, &cmvy);

                    int csx = cpx + cmvx, csy = cpy + cmvy;
                    if (csx < 0) csx = 0;
                    if (csy < 0) csy = 0;
                    if (csx + 4 > w/2) csx = w/2 - 4;
                    if (csy + 4 > h/2) csy = h/2 - 4;

                    /* Cb residual */
                    for (int y = 0; y < 4; y++)
                        for (int x = 0; x < 4; x++)
                            block[y * 4 + x] = cur->cb[(cpy+y)*cur->cb_stride+(cpx+x)]
                                - ref->cb[(csy+y)*ref->cb_stride+(csx+x)];

                    blunt_dct4x4_block(block, coeff);
                    for (int i = 0; i < 16; i++) {
                        int q = blunt_chroma_quant[qp][i];
                        if (q == 0) q = 1;
                        coeff[i] = (int16_t)((coeff[i] + (q >> 1)) / q);
                    }
                    for (int i = 0; i < 16; i++)
                        zigzag[i] = coeff[blunt_zigzag_scan[i]];

                    uint32_t cmv_enc = ((uint32_t)(cmvy & 0xFF) << 8) | (uint32_t)(cmvx & 0xFF);
                    bw_write(bw, cmv_enc, 16);
                    encode_block_runlevel(bw, zigzag);

                    /* Cr residual */
                    for (int y = 0; y < 4; y++)
                        for (int x = 0; x < 4; x++)
                            block[y * 4 + x] = cur->cr[(cpy+y)*cur->cr_stride+(cpx+x)]
                                - ref->cr[(csy+y)*ref->cr_stride+(csx+x)];

                    blunt_dct4x4_block(block, coeff);
                    for (int i = 0; i < 16; i++) {
                        int q = blunt_chroma_quant[qp][i];
                        if (q == 0) q = 1;
                        coeff[i] = (int16_t)((coeff[i] + (q >> 1)) / q);
                    }
                    for (int i = 0; i < 16; i++)
                        zigzag[i] = coeff[blunt_zigzag_scan[i]];

                    bw_write(bw, cmv_enc, 16);
                    encode_block_runlevel(bw, zigzag);
                }
            }
        }
    }
    return 0;
}

// encoder state
struct BluntEncoder {
    BluntHeader  hdr;
    FILE        *fp;
    uint32_t     frame_count;
    uint8_t      quality;

    BluntFrame   ref_frame;
    int          has_reference;
    int          keyframe_interval;

    int          audio_enabled;
    int          audio_channels;
    int          audio_sample_rate;
    long         last_frame_hdr_pos;
};

BluntEncoder *blunt_encoder_create(void) {
    BluntEncoder *enc = (BluntEncoder *)calloc(1, sizeof(BluntEncoder));
    if (!enc) return NULL;
    enc->quality = 75;
    enc->keyframe_interval = 30;
    return enc;
}

void blunt_encoder_destroy(BluntEncoder *enc) {
    if (!enc) return;
    if (enc->fp) fclose(enc->fp);
    blunt_frame_free(&enc->ref_frame);
    free(enc);
}

int blunt_encoder_set_quality(BluntEncoder *enc, uint8_t quality) {
    if (quality < 1 || quality > 100) return -1;
    enc->quality = quality;
    return 0;
}

int blunt_encoder_set_audio(BluntEncoder *enc, int channels, int sample_rate) {
    if (channels < 0 || channels > 2) return -1;
    if (sample_rate < 8000 || sample_rate > 96000) return -1;
    enc->audio_enabled = 1;
    enc->audio_channels = channels;
    enc->audio_sample_rate = sample_rate;
    return 0;
}

int blunt_encoder_open(BluntEncoder *enc, const char *path,
                       uint16_t width, uint16_t height,
                       uint16_t fps_num, uint16_t fps_den) {
    enc->fp = fopen(path, "wb");
    if (!enc->fp) return -1;

    int mbw = (width + BLUNT_MB_SIZE - 1) / BLUNT_MB_SIZE;
    int mbh = (height + BLUNT_MB_SIZE - 1) / BLUNT_MB_SIZE;

    memset(&enc->hdr, 0, sizeof(enc->hdr));
    memcpy(enc->hdr.magic, BLUNT_MAGIC, 4);
    enc->hdr.version = BLUNT_VERSION;
    enc->hdr.header_size = BLUNT_HEADER_SIZE;
    enc->hdr.width = width;
    enc->hdr.height = height;
    enc->hdr.fps_num = fps_num;
    enc->hdr.fps_den = fps_den;
    enc->hdr.num_frames = 0;
    enc->hdr.mb_width = (uint16_t)mbw;
    enc->hdr.mb_height = (uint16_t)mbh;
    enc->hdr.flags = 0;
    if (enc->audio_enabled)
        enc->hdr.flags |= BLUNT_FLAG_HAS_AUDIO;
    enc->hdr.quality = enc->quality;
    enc->hdr.num_keyframes = 0;

    if (enc->audio_enabled) {
        enc->hdr.audio_sample_rate = (uint16_t)enc->audio_sample_rate;
        enc->hdr.audio_channels = (uint8_t)enc->audio_channels;
        enc->hdr.audio_bits = 16;
    }

    /* Write placeholder header */
    fwrite(&enc->hdr, sizeof(enc->hdr), 1, enc->fp);
    fflush(enc->fp);

    enc->frame_count = 0;
    enc->has_reference = 0;

    blunt_init_quant_tables(enc->quality);
    blunt_init_audio_quant(enc->quality);
    blunt_frame_alloc(&enc->ref_frame, mbw, mbh);

    return 0;
}

int blunt_encoder_write_frame(BluntEncoder *enc, const BluntFrame *frame,
                              int force_keyframe) {
    int w = enc->hdr.width;
    int h = enc->hdr.height;
    int mbw = enc->hdr.mb_width;
    int mbh = enc->hdr.mb_height;
    int qp = ((100 - enc->quality) * BLUNT_MAX_QP) / 100;
    if (qp < 1) qp = 1;

    int is_keyframe = force_keyframe || !enc->has_reference ||
                      (enc->frame_count % enc->keyframe_interval == 0);

    /* Encode frame */
    size_t buf_cap = w * h * 4;
    BitWriter bw;
    bw_init(&bw, buf_cap);

    if (is_keyframe) {
        encode_frame_i(&bw, frame, mbw, mbh, qp);
    } else {
        encode_frame_p(&bw, frame, &enc->ref_frame, mbw, mbh, w, h, qp);
    }

    size_t data_size = bw_flush(&bw);

    /* Write frame header */
    BluntFrameHeader fhdr;
    fhdr.frame_num = enc->frame_count;
    fhdr.frame_type = is_keyframe ? BLUNT_FRAME_I : BLUNT_FRAME_P;
    fhdr.data_size = (uint32_t)data_size;
    fhdr.timestamp_ms = (uint32_t)((uint64_t)enc->frame_count * enc->hdr.fps_den * 1000 / enc->hdr.fps_num);
    fhdr.ref_frame = is_keyframe ? 0 : (uint16_t)(enc->frame_count - 1);
    fhdr.audio_data_size = 0;

    enc->last_frame_hdr_pos = ftell(enc->fp);
    fwrite(&fhdr, sizeof(fhdr), 1, enc->fp);
    fwrite(bw.data, 1, data_size, enc->fp);
    fflush(enc->fp);

    /* Update reference */
    memcpy(enc->ref_frame.y, frame->y, frame->y_stride * h);
    memcpy(enc->ref_frame.cb, frame->cb, frame->cb_stride * ((h+1)/2));
    memcpy(enc->ref_frame.cr, frame->cr, frame->cr_stride * ((h+1)/2));
    enc->has_reference = 1;

    enc->frame_count++;

    free(bw.data);
    return 0;
}

int blunt_encoder_write_audio_frame(BluntEncoder *enc,
                                    const int16_t *samples, int num_samples) {
    if (!enc->audio_enabled || !enc->fp) return -1;
    if (!samples || num_samples <= 0) return -1;

    int qp = ((100 - enc->quality) * 63) / 100;
    if (qp < 0) qp = 0;

    uint8_t buf[65536];
    int sz = blunt_audio_encode_frame(samples, num_samples,
                                      enc->audio_channels, qp,
                                      buf, sizeof(buf));
    if (sz < 0) return -1;

    /* Seek back and update audio_data_size in frame header */
    long cur = ftell(enc->fp);
    fseek(enc->fp, enc->last_frame_hdr_pos + 15, SEEK_SET);
    uint16_t asz = (uint16_t)sz;
    fwrite(&asz, 2, 1, enc->fp);
    fseek(enc->fp, cur, SEEK_SET);

    /* Write audio data */
    fwrite(buf, 1, sz, enc->fp);
    fflush(enc->fp);
    return 0;
}

int blunt_encoder_close(BluntEncoder *enc) {
    if (!enc->fp) return -1;

    /* Update header with final frame count */
    enc->hdr.num_frames = enc->frame_count;
    fseek(enc->fp, 0, SEEK_SET);
    fwrite(&enc->hdr, sizeof(enc->hdr), 1, enc->fp);
    fclose(enc->fp);
    enc->fp = NULL;

    return 0;
}

// frame allocation and color conversion
int blunt_frame_alloc(BluntFrame *f, int mb_width, int mb_height) {
    int w = mb_width * BLUNT_MB_SIZE;
    int h = mb_height * BLUNT_MB_SIZE;
    int cw = (w + 1) / 2;
    int ch = (h + 1) / 2;

    f->y_stride = w;
    f->cb_stride = cw;
    f->cr_stride = cw;

    f->y  = (uint8_t *)calloc(1, f->y_stride * h);
    f->cb = (uint8_t *)calloc(1, f->cb_stride * ch);
    f->cr = (uint8_t *)calloc(1, f->cr_stride * ch);

    if (!f->y || !f->cb || !f->cr) {
        blunt_frame_free(f);
        return -1;
    }
    return 0;
}

void blunt_frame_free(BluntFrame *f) {
    free(f->y);   f->y = NULL;
    free(f->cb);  f->cb = NULL;
    free(f->cr);  f->cr = NULL;
}

int blunt_rgb_to_ycbcr(const uint8_t *rgb, int w, int h, int stride,
                        BluntFrame *out) {
    int mbw = (w + BLUNT_MB_SIZE - 1) / BLUNT_MB_SIZE;
    int mbh = (h + BLUNT_MB_SIZE - 1) / BLUNT_MB_SIZE;
    int padded_w = mbw * BLUNT_MB_SIZE;
    int padded_h = mbh * BLUNT_MB_SIZE;
    int cw = (padded_w + 1) / 2;
    int ch = (padded_h + 1) / 2;

    if (out->y == NULL) {
        blunt_frame_alloc(out, mbw, mbh);
    }

    blunt_rgb_to_ycbcr_plane(rgb, out->y, out->cb, out->cr,
                              w, h, stride,
                              out->y_stride, out->cb_stride, out->cr_stride);

    /* Zero-pad */
    for (int row = h; row < padded_h; row++)
        memset(out->y + row * out->y_stride, 16, padded_w);
    for (int row = (h+1)/2; row < ch; row++) {
        memset(out->cb + row * out->cb_stride, 128, cw);
        memset(out->cr + row * out->cr_stride, 128, cw);
    }

    return 0;
}

int blunt_ycbcr_to_rgb(const BluntFrame *f, uint8_t *rgb, int w, int h,
                       int stride) {
    blunt_ycbcr_to_rgb_plane(f->y, f->cb, f->cr, rgb,
                             w, h, f->y_stride, f->cb_stride, f->cr_stride,
                             stride);
    return 0;
}

const char *blunt_version_string(void) {
    return "BLUNT Codec v1.0 (SIMD-accelerated)";
}
