#include "blunt.h"
#include "blunt_tables.h"
#include "blunt_simd.h"
#include "blunt_audio.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// bitstream reader
typedef struct {
    const uint8_t *data;
    size_t         len;
    size_t         byte_pos;
    int            bit_pos;
} BitReader;

static void br_init(BitReader *br, const uint8_t *data, size_t len) {
    br->data = data;
    br->len = len;
    br->byte_pos = 0;
    br->bit_pos = 0;
}

static uint32_t br_read(BitReader *br, int nbits) {
    uint32_t val = 0;
    for (int i = 0; i < nbits; i++) {
        if (br->byte_pos >= br->len) return val;
        val <<= 1;
        val |= (br->data[br->byte_pos] >> (7 - br->bit_pos)) & 1;
        br->bit_pos++;
        if (br->bit_pos >= 8) {
            br->bit_pos = 0;
            br->byte_pos++;
        }
    }
    return val;
}

// decoder internal state
struct BluntDecoder {
    BluntHeader  hdr;
    FILE        *fp;
    int          header_read;
    int          has_reference;

    BluntFrame   ref_frame;
    BluntFrame   cur_frame;
    int16_t     *dct_buffer;

    BluntProgressFn progress;
    void            *progress_ud;

    uint8_t     *audio_buf;
    int          audio_buf_size;
    int          audio_valid;
};

// frame decoding
static int decode_macroblock_i(BitReader *br, int16_t *block,
                               const int16_t *qtable) {
    int idx = 0;
    while (idx < 16) {
        uint32_t run_level = br_read(br, 8);
        int run  = (run_level >> 4) & 0xF;
        int level = run_level & 0xF;
        if (level == 0 && run == 0) break;
        if (level == 0xF) {
            int32_t raw = (int32_t)(int16_t)(uint16_t)br_read(br, 16);
            idx += run;
            if (idx < 16) {
                int lin = blunt_zigzag_scan[idx];
                int q = qtable[lin];
                block[lin] = (int16_t)(raw * q);
            }
            idx++;
        } else {
            int sign = br_read(br, 1);
            int val = level;
            if (sign) val = -val;
            idx += run;
            if (idx < 16) {
                int lin = blunt_zigzag_scan[idx];
                int q = qtable[lin];
                block[lin] = (int16_t)(val * q);
            }
            idx++;
        }
    }
    return 0;
}

static int decode_macroblock_p(BitReader *br, int16_t *block,
                               int *mvx, int *mvy,
                               const int16_t *qtable) {
    /* Motion vector */
    uint32_t mv_raw = br_read(br, 16);
    int dx = (int)(mv_raw & 0xFF);
    int dy = (int)((mv_raw >> 8) & 0xFF);
    if (dx >= 128) dx -= 256;
    if (dy >= 128) dy -= 256;
    *mvx = dx;
    *mvy = dy;

    /* Residual */
    return decode_macroblock_i(br, block, qtable);
}

static void copy_block_to_plane(uint8_t *plane, int stride,
                                const int16_t *block, int bx, int by) {
    for (int y = 0; y < 4; y++) {
        for (int x = 0; x < 4; x++) {
            int val = block[y * 4 + x];
            int px = bx + x;
            int py = by + y;
            plane[py * stride + px] = (uint8_t)(val < 0 ? 0 : (val > 255 ? 255 : val));
        }
    }
}

// public api
BluntDecoder *blunt_decoder_create(void) {
    BluntDecoder *dec = (BluntDecoder *)calloc(1, sizeof(BluntDecoder));
    if (!dec) return NULL;
    dec->dct_buffer = (int16_t *)malloc(16 * 16 * sizeof(int16_t));
    dec->audio_buf = (uint8_t *)malloc(65536);
    if (!dec->dct_buffer || !dec->audio_buf) {
        free(dec->dct_buffer);
        free(dec->audio_buf);
        free(dec);
        return NULL;
    }
    return dec;
}

void blunt_decoder_destroy(BluntDecoder *dec) {
    if (!dec) return;
    if (dec->fp) fclose(dec->fp);
    blunt_frame_free(&dec->ref_frame);
    blunt_frame_free(&dec->cur_frame);
    free(dec->dct_buffer);
    free(dec->audio_buf);
    free(dec);
}

int blunt_decoder_open(BluntDecoder *dec, const char *path) {
    dec->fp = fopen(path, "rb");
    if (!dec->fp) return -1;

    BluntHeader hdr;
    if (fread(&hdr, sizeof(hdr), 1, dec->fp) != 1)
        return -1;

    if (memcmp(hdr.magic, BLUNT_MAGIC, 4) != 0)
        return -1;
    if (hdr.version != BLUNT_VERSION)
        return -1;

    dec->hdr = hdr;
    dec->header_read = 1;

    blunt_init_quant_tables(dec->hdr.quality);
    blunt_init_audio_quant(dec->hdr.quality);

    int mbw = (dec->hdr.width + BLUNT_MB_SIZE - 1) / BLUNT_MB_SIZE;
    int mbh = (dec->hdr.height + BLUNT_MB_SIZE - 1) / BLUNT_MB_SIZE;
    dec->hdr.mb_width = (uint16_t)mbw;
    dec->hdr.mb_height = (uint16_t)mbh;

    blunt_frame_alloc(&dec->ref_frame, mbw, mbh);
    blunt_frame_alloc(&dec->cur_frame, mbw, mbh);

    return 0;
}

int blunt_decoder_read_header(BluntDecoder *dec, BluntHeader *hdr) {
    if (!dec->header_read) return -1;
    *hdr = dec->hdr;
    return 0;
}

void blunt_decoder_set_progress(BluntDecoder *dec, BluntProgressFn fn, void *ud) {
    dec->progress = fn;
    dec->progress_ud = ud;
}

static int decode_one_frame(BluntDecoder *dec, BluntFrame *out) {
    BluntFrameHeader fhdr;
    if (fread(&fhdr, sizeof(fhdr), 1, dec->fp) != 1)
        return -1;

    size_t data_size = fhdr.data_size;
    uint8_t *buf = (uint8_t *)malloc(data_size);
    if (!buf) return -1;
    if (fread(buf, 1, data_size, dec->fp) != (size_t)data_size) {
        free(buf);
        return -1;
    }

    BitReader br;
    br_init(&br, buf, data_size);

    int mbw = dec->hdr.mb_width;
    int mbh = dec->hdr.mb_height;
    int w = dec->hdr.width;
    int h = dec->hdr.height;
    int qp = ((100 - dec->hdr.quality) * BLUNT_MAX_QP) / 100;
    if (qp < 0) qp = 0;
    if (qp > BLUNT_MAX_QP) qp = BLUNT_MAX_QP;

    /* Reset current frame */
    memset(out->y, 0, out->y_stride * h);
    memset(out->cb, 0, out->cb_stride * ((h + 1) / 2));
    memset(out->cr, 0, out->cr_stride * ((h + 1) / 2));

    int16_t *block = dec->dct_buffer;

    if (fhdr.frame_type == BLUNT_FRAME_I) {
        /* Intra-coded frame */
        for (int mb_y = 0; mb_y < mbh; mb_y++) {
            for (int mb_x = 0; mb_x < mbw; mb_x++) {
                /* Luma: 16 sub-blocks of 4x4 */
                for (int by = 0; by < 4; by++) {
                    for (int bx = 0; bx < 4; bx++) {
                        memset(block, 0, 16 * sizeof(int16_t));
                        decode_macroblock_i(&br, block, blunt_luma_quant[qp]);
                        blunt_idct4x4_block(block);
                        int px = mb_x * 16 + bx * 4;
                        int py = mb_y * 16 + by * 4;
                        copy_block_to_plane(out->y, out->y_stride, block, px, py);
                    }
                }
                /* Chroma Cb: 4 sub-blocks of 4x4 (2x2 in chroma space) */
                for (int cby = 0; cby < 2; cby++) {
                    for (int cbx = 0; cbx < 2; cbx++) {
                        memset(block, 0, 16 * sizeof(int16_t));
                        decode_macroblock_i(&br, block, blunt_chroma_quant[qp]);
                        blunt_idct4x4_block(block);
                        copy_block_to_plane(out->cb, out->cb_stride, block,
                                            mb_x * 8 + cbx * 4, mb_y * 8 + cby * 4);
                    }
                }
                /* Chroma Cr: 4 sub-blocks of 4x4 */
                for (int cby = 0; cby < 2; cby++) {
                    for (int cbx = 0; cbx < 2; cbx++) {
                        memset(block, 0, 16 * sizeof(int16_t));
                        decode_macroblock_i(&br, block, blunt_chroma_quant[qp]);
                        blunt_idct4x4_block(block);
                        copy_block_to_plane(out->cr, out->cr_stride, block,
                                            mb_x * 8 + cbx * 4, mb_y * 8 + cby * 4);
                    }
                }
            }
        }
    } else if (fhdr.frame_type == BLUNT_FRAME_P && dec->has_reference) {
        /* Predicted frame */
        for (int mb_y = 0; mb_y < mbh; mb_y++) {
            for (int mb_x = 0; mb_x < mbw; mb_x++) {
                int mvx, mvy;

                /* Luma: 16 sub-blocks of 4x4 with motion compensation */
                for (int by = 0; by < 4; by++) {
                    for (int bx = 0; bx < 4; bx++) {
                        memset(block, 0, 16 * sizeof(int16_t));
                        decode_macroblock_p(&br, block, &mvx, &mvy, blunt_luma_quant[qp]);
                        blunt_idct4x4_block(block);
                        int px = mb_x * 16 + bx * 4;
                        int py = mb_y * 16 + by * 4;

                        int sx = px + mvx, sy = py + mvy;
                        if (sx < 0) sx = 0;
                        if (sy < 0) sy = 0;
                        if (sx + 4 > w) sx = w - 4;
                        if (sy + 4 > h) sy = h - 4;

                        for (int i = 0; i < 4; i++)
                            for (int j = 0; j < 4; j++) {
                                int v = dec->ref_frame.y[(sy+i)*dec->ref_frame.y_stride+(sx+j)] + block[i*4+j];
                                out->y[(py+i)*out->y_stride+(px+j)] =
                                    (uint8_t)(v < 0 ? 0 : (v > 255 ? 255 : v));
                            }
                    }
                }

                /* Chroma: 4 sub-blocks of 4x4 (2x2 in chroma space) */
                for (int cby = 0; cby < 2; cby++) {
                    for (int cbx = 0; cbx < 2; cbx++) {
                        int mvx_c, mvy_c;
                        int cx = mb_x * 8 + cbx * 4, cy = mb_y * 8 + cby * 4;

                        memset(block, 0, 16 * sizeof(int16_t));
                        decode_macroblock_p(&br, block, &mvx_c, &mvy_c, blunt_chroma_quant[qp]);
                        blunt_idct4x4_block(block);

                        int csx = cx + mvx_c, csy = cy + mvy_c;
                        if (csx < 0) csx = 0;
                        if (csy < 0) csy = 0;
                        if (csx + 4 > w/2) csx = w/2 - 4;
                        if (csy + 4 > h/2) csy = h/2 - 4;

                        for (int i = 0; i < 4; i++)
                            for (int j = 0; j < 4; j++) {
                                int v = dec->ref_frame.cb[(csy+i)*dec->ref_frame.cb_stride+(csx+j)] + block[i*4+j];
                                out->cb[(cy+i)*out->cb_stride+(cx+j)] =
                                    (uint8_t)(v < 0 ? 0 : (v > 255 ? 255 : v));
                            }

                        memset(block, 0, 16 * sizeof(int16_t));
                        decode_macroblock_p(&br, block, &mvx_c, &mvy_c, blunt_chroma_quant[qp]);
                        blunt_idct4x4_block(block);

                        csx = cx + mvx_c; csy = cy + mvy_c;
                        if (csx < 0) csx = 0;
                        if (csy < 0) csy = 0;
                        if (csx + 4 > w/2) csx = w/2 - 4;
                        if (csy + 4 > h/2) csy = h/2 - 4;

                        for (int i = 0; i < 4; i++)
                            for (int j = 0; j < 4; j++) {
                                int v = dec->ref_frame.cr[(csy+i)*dec->ref_frame.cr_stride+(csx+j)] + block[i*4+j];
                                out->cr[(cy+i)*out->cr_stride+(cx+j)] =
                                    (uint8_t)(v < 0 ? 0 : (v > 255 ? 255 : v));
                            }
                    }
                }
            }
        }
    } else {
        free(buf);
        return -1;
    }

    free(buf);

    /* Read audio data if present */
    dec->audio_valid = 0;
    if ((dec->hdr.flags & BLUNT_FLAG_HAS_AUDIO) && fhdr.audio_data_size > 0) {
        int asz = (int)fhdr.audio_data_size;
        if (asz <= 65536 && (int)fread(dec->audio_buf, 1, asz, dec->fp) == asz) {
            dec->audio_buf_size = asz;
            dec->audio_valid = 1;
        }
    }

    return 0;
}

int blunt_decoder_read_frame(BluntDecoder *dec, BluntFrame *out) {
    int mbw = dec->hdr.mb_width;
    int mbh = dec->hdr.mb_height;
    int h = dec->hdr.height;

    if (out->y == NULL) {
        blunt_frame_alloc(out, mbw, mbh);
    }

    int ret = decode_one_frame(dec, out);

    if (ret == 0) {
        /* Copy to reference for P-frame prediction */
        memcpy(dec->ref_frame.y, out->y, out->y_stride * h);
        memcpy(dec->ref_frame.cb, out->cb, out->cb_stride * ((h+1)/2));
        memcpy(dec->ref_frame.cr, out->cr, out->cr_stride * ((h+1)/2));
        dec->has_reference = 1;
    }

    return ret;
}

int blunt_decoder_read_audio_frame(BluntDecoder *dec, int16_t *out,
                                   int max_samples) {
    if (!dec->audio_valid || !dec->audio_buf) return 0;
    if (!(dec->hdr.flags & BLUNT_FLAG_HAS_AUDIO)) return 0;

    int channels = dec->hdr.audio_channels;
    if (channels <= 0) return 0;

    int qp = ((100 - dec->hdr.quality) * 63) / 100;
    if (qp < 0) qp = 0;
    if (qp > 63) qp = 63;

    return blunt_audio_decode_frame(dec->audio_buf, dec->audio_buf_size,
                                    channels, qp, out, max_samples);
}

int blunt_decoder_decode_frame(BluntDecoder *dec, uint32_t frame_num, BluntFrame *out) {
    if (!dec->header_read) return -1;

    int mbw = dec->hdr.mb_width;
    int mbh = dec->hdr.mb_height;

    if (out->y == NULL)
        blunt_frame_alloc(out, mbw, mbh);

    /* Seek to frame */
    fseek(dec->fp, BLUNT_HEADER_SIZE + (long)frame_num * (BLUNT_FRAME_HDR_SIZE + 1024),
          SEEK_SET);

    dec->has_reference = 0;
    /* Decode keyframes leading up to target */
    for (uint32_t i = 0; i <= frame_num && i < dec->hdr.num_frames; i++) {
        int mbw2 = dec->hdr.mb_width, mbh2 = dec->hdr.mb_height;
        (void)mbw2; (void)mbh2;
        int ret = decode_one_frame(dec, out);
        if (ret != 0) return ret;

        memcpy(dec->ref_frame.y, out->y, out->y_stride * dec->hdr.height);
        memcpy(dec->ref_frame.cb, out->cb, out->cb_stride * ((dec->hdr.height+1)/2));
        memcpy(dec->ref_frame.cr, out->cr, out->cr_stride * ((dec->hdr.height+1)/2));
        dec->has_reference = 1;

        if (dec->progress)
            dec->progress((int)i + 1, (int)frame_num + 1, dec->progress_ud);
    }

    return 0;
}

void blunt_decoder_seek(BluntDecoder *dec, uint32_t frame_num) {
    fseek(dec->fp, BLUNT_HEADER_SIZE + (long)frame_num * (BLUNT_FRAME_HDR_SIZE + 1024),
          SEEK_SET);
    dec->has_reference = 0;
}
