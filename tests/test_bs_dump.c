#include "blunt_audio.h"
#include "blunt_tables.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

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

    uint8_t bs[65536];
    int bsz = blunt_audio_encode_frame(in, nsamp, 2, qp, bs, sizeof(bs));

    printf("bitstream %d bytes, hex dump around ch1 start:\n", bsz);
    // ch0 is 2 blocks, ch1 starts after ch0's 2 blocks
    // print all bytes with bit position markers
    for (int i = 0; i < bsz && i < 60; i++) {
        printf("  [%3d] 0x%02X = %3d  bits %d-%d\n", i, bs[i], bs[i], i*8, i*8+7);
    }

    // now also do a direct bit-level decode of the first few entries
    printf("\ndirect bitstream decode:\n");
    int byte_pos = 0, bit_pos = 0;
    for (int block = 0; block < 4; block++) {
        int ch = block / 2;
        int b = block % 2;
        printf("block ch%d b%d (starts at byte %d bit %d):\n", ch, b, byte_pos, bit_pos);
        int idx = 0;
        while (idx < 16) {
            // read 8-bit header
            uint32_t rl = 0;
            for (int i = 0; i < 8; i++) {
                if (byte_pos >= bsz) break;
                rl <<= 1;
                rl |= (bs[byte_pos] >> (7 - bit_pos)) & 1;
                bit_pos++;
                if (bit_pos >= 8) { bit_pos = 0; byte_pos++; }
            }
            int run = (rl >> 4) & 0xF;
            int level = rl & 0xF;
            if (level == 0 && run == 0) {
                printf("  [%2d] EOB (byte %d bit %d)\n", idx, byte_pos, bit_pos);
                break;
            }
            if (level == 0xF) {
                uint32_t val = 0;
                for (int i = 0; i < 32; i++) {
                    if (byte_pos >= bsz) break;
                    val <<= 1;
                    val |= (bs[byte_pos] >> (7 - bit_pos)) & 1;
                    bit_pos++;
                    if (bit_pos >= 8) { bit_pos = 0; byte_pos++; }
                }
                idx += run;
                printf("  [%2d] ESC32 run=%d val=%d (byte %d bit %d)\n", idx, run, (int)val, byte_pos, bit_pos);
                idx++;
            } else if (level == 0xE) {
                uint32_t val = 0;
                for (int i = 0; i < 16; i++) {
                    if (byte_pos >= bsz) break;
                    val <<= 1;
                    val |= (bs[byte_pos] >> (7 - bit_pos)) & 1;
                    bit_pos++;
                    if (bit_pos >= 8) { bit_pos = 0; byte_pos++; }
                }
                idx += run;
                printf("  [%2d] ESC16 run=%d val=%d (byte %d bit %d)\n", idx, run, (int)(int16_t)(uint16_t)val, byte_pos, bit_pos);
                idx++;
            } else {
                uint32_t sign = 0;
                for (int i = 0; i < 1; i++) {
                    if (byte_pos >= bsz) break;
                    sign <<= 1;
                    sign |= (bs[byte_pos] >> (7 - bit_pos)) & 1;
                    bit_pos++;
                    if (bit_pos >= 8) { bit_pos = 0; byte_pos++; }
                }
                int val = sign ? -level : level;
                idx += run;
                printf("  [%2d] run=%d level=%d sign=%d -> %d (byte %d bit %d)\n", idx, run, level, sign, val, byte_pos, bit_pos);
                idx++;
            }
        }
    }

    return 0;
}
