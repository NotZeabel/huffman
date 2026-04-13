#include <stdint.h>
#include <stdio.h>

typedef struct {
    FILE *f;
    uint8_t buffer;
    int bits;
} BitWriter;

void bw_init(BitWriter *bw, FILE *f) {
    bw->f = f;
    bw->buffer = 0;
    bw->bits = 0;
}

void bw_write_bit(BitWriter *bw, int bit) {
    bw->buffer |= bit << (7 - bw->bits);
    bw->bits++;

    if (bw->bits == 8) {
        fputc(bw->buffer, bw->f);
        bw->buffer = 0;
        bw->bits = 0;
    }
}

void bw_write_code(BitWriter *bw, char *code) {
    for (int i = 0; code[i]; i++) {
        bw_write_bit(bw, code[i] == '1');
    }
}

void bw_flush(BitWriter *bw) {
    if (bw->bits) {
        fputc(bw->buffer, bw->f);
    }
}
