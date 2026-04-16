#include <stdint.h>
#include <stdio.h>

typedef struct {
    FILE *f;
    uint8_t buffer;
    int bits;
} BitReader;

void br_init(BitReader *br, FILE *f) {
    br->f = f;
    br->buffer = 0;
    br->bits = 0;
}

int br_read_bit(BitReader *br) {
    if (br->bits == 0) {
        br->buffer = fgetc(br->f);
        br->bits = 8;
    }

    int bit = (br->buffer >> (br->bits - 1)) & 1;
    br->bits--;

    return bit;
}
