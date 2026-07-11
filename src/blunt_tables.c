#include "blunt.h"
#include "blunt_tables.h"

const int blunt_zigzag_scan[16] = {
     0,  1,  4,  8,
     5,  2,  3,  6,
     9, 12, 13, 10,
     7, 11, 14, 15
};

const int blunt_dezigzag_scan[16] = {
     0,  1,  5,  6,
     2,  4,  7, 12,
     3,  8, 11, 13,
     9, 10, 14, 15
};

static const int default_luma_qmatrix[16] = {
    16, 11, 10, 16,
    12, 12, 14, 19,
    14, 14, 18, 23,
    18, 22, 23, 24
};

static const int default_chroma_qmatrix[16] = {
    17, 18, 24, 47,
    26, 29, 40, 51,
    29, 35, 43, 54,
    37, 46, 56, 61
};

int16_t blunt_luma_quant[BLUNT_MAX_QP + 1][BLUNT_BLOCK_COEFFS];
int16_t blunt_chroma_quant[BLUNT_MAX_QP + 1][BLUNT_BLOCK_COEFFS];

static int clamp_int16(int v) {
    if (v < -32768) return -32768;
    if (v >  32767) return  32767;
    return v;
}

void blunt_init_quant_tables(int quality) {
    if (quality < 1) quality = 1;
    if (quality > 100) quality = 100;

    int scale = (quality < 50) ? (5000 / quality) : (200 - 2 * quality);

    for (int qp = 0; qp <= BLUNT_MAX_QP; qp++) {
        for (int i = 0; i < 16; i++) {
            int lq = (default_luma_qmatrix[i] * scale + 50) / 100;
            int cq = (default_chroma_qmatrix[i] * scale + 50) / 100;
            if (lq < 1) lq = 1;
            if (lq > 255) lq = 255;
            if (cq < 1) cq = 1;
            if (cq > 255) cq = 255;

            int qp_adj = (qp >> 1) + (qp & 1);
            lq = clamp_int16((lq * qp_adj + 2) >> 2);
            cq = clamp_int16((cq * qp_adj + 2) >> 2);
            if (lq < 1) lq = 1;
            if (cq < 1) cq = 1;

            blunt_luma_quant[qp][blunt_zigzag_scan[i]] = (int16_t)lq;
            blunt_chroma_quant[qp][blunt_zigzag_scan[i]] = (int16_t)cq;
        }
    }
}

void blunt_huff_build_lookup(const uint16_t *freq, int nsym,
                             BluntHuffTable *tbl) {
    int bl_count[BLUNT_HUFF_MAX_BITS + 1];
    int code;
    int maxbits = 0;

    for (int i = 0; i <= BLUNT_HUFF_MAX_BITS; i++)
        bl_count[i] = 0;

    for (int i = 0; i < nsym; i++)
        tbl->bits[i] = 0;

    tbl->max_sym = nsym - 1;

    /* Count bit lengths using the canonical method */
    for (int i = 0; i < nsym; i++) {
        if (freq[i] == 0) continue;
        int b = 0;
        uint32_t f = freq[i];
        while (f > 1) { f >>= 1; b++; }
        if (b == 0) b = 1;
        if (b > BLUNT_HUFF_MAX_BITS) b = BLUNT_HUFF_MAX_BITS;
        tbl->bits[i] = (uint8_t)b;
        bl_count[b]++;
        if (b > maxbits) maxbits = b;
    }

    code = 0;
    int next_code[BLUNT_HUFF_MAX_BITS + 1];
    next_code[0] = 0;
    for (int bits = 1; bits <= maxbits; bits++) {
        code = (code + bl_count[bits - 1]) << 1;
        next_code[bits] = code;
    }

    for (int i = 0; i < nsym; i++) {
        if (tbl->bits[i] == 0) {
            tbl->codes[i] = 0;
            continue;
        }
        tbl->codes[i] = (uint16_t)next_code[tbl->bits[i]]++;
    }
}
