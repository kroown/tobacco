#ifndef BLUNT_TABLES_H
#define BLUNT_TABLES_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BLUNT_ZIGZAG_SIZE 16

extern const int blunt_zigzag_scan[BLUNT_ZIGZAG_SIZE];
extern const int blunt_dezigzag_scan[BLUNT_ZIGZAG_SIZE];

#define BLUNT_MAX_QP     51
#define BLUNT_BLOCK_COEFFS 16

void blunt_init_quant_tables(int quality);

extern int16_t blunt_luma_quant[BLUNT_MAX_QP + 1][BLUNT_BLOCK_COEFFS];
extern int16_t blunt_chroma_quant[BLUNT_MAX_QP + 1][BLUNT_BLOCK_COEFFS];

#define BLUNT_HUFF_SYMBOLS   256
#define BLUNT_HUFF_MAX_BITS  16

typedef struct {
    uint8_t  bits[BLUNT_HUFF_SYMBOLS];
    uint16_t codes[BLUNT_HUFF_SYMBOLS];
    int      max_sym;
} BluntHuffTable;

void blunt_huff_build_lookup(const uint16_t *freq, int nsym,
                             BluntHuffTable *tbl);

#ifdef __cplusplus
}
#endif

#endif
