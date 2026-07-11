#include "blunt_audio.h"
#include "blunt_tables.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

int main(void) {
    blunt_init_audio_quant(80);
    int qp = ((100 - 80) * 63) / 100;

    int nsamp = 16;
    int16_t in[32];
    for (int i = 0; i < nsamp; i++) {
        double t = (double)i / 44100.0;
        double val = sin(2.0 * 3.14159265358979 * 440.0 * t) * 16000.0;
        in[i*2+0] = (int16_t)val;
        in[i*2+1] = (int16_t)(val * 0.7);
    }

    // encode ch0 block manually to see how many non-zero quantized coeffs
    int32_t coded[16];
    int16_t block[16];
    for (int i = 0; i < 16; i++) block[i] = in[i*2+0]; // ch0
    blunt_audio_encode_block(block, coded, qp);
    int nz = 0;
    printf("ch0 quantized: ");
    for (int i = 0; i < 16; i++) {
        printf("%d ", coded[i]);
        if (coded[i] != 0) nz++;
    }
    printf("\nch0 non-zero count: %d\n", nz);

    for (int i = 0; i < 16; i++) block[i] = in[i*2+1]; // ch1
    blunt_audio_encode_block(block, coded, qp);
    nz = 0;
    printf("ch1 quantized: ");
    for (int i = 0; i < 16; i++) {
        printf("%d ", coded[i]);
        if (coded[i] != 0) nz++;
    }
    printf("\nch1 non-zero count: %d\n", nz);

    return 0;
}
