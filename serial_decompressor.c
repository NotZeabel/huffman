#include "utils/bitreader.c"
#include "utils/heap.c"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

// ===== DEFINITIONS =====

#define SYMBOLS 256
#define MAX_FILES 1024

typedef struct {
    char path[512];
    uint64_t size;
} FileEntry;

// ===== MORE HUFFMAN SUTFF =====

Node *build_huffman(uint64_t freq[SYMBOLS]) {
    for (int i = 0; i < SYMBOLS; i++) {
        if (freq[i] == 0)
            continue;
        Node *n = malloc(sizeof(Node));
        n->symbol = i;
        n->freq = freq[i];
        n->left = NULL;
        n->right = NULL;

        heap_push(n);
    }

    while (heap_size > 1) {
        Node *a = heap_pop();
        Node *b = heap_pop();

        Node *parent = malloc(sizeof(Node));
        parent->symbol = 0;
        parent->freq = a->freq + b->freq;
        parent->left = a;
        parent->right = b;

        heap_push(parent);
    }

    return heap_pop();
}

// ===== FILE SHENANIGANS =====

void read_header(FILE *f, FileEntry files[], int *file_count,
                 uint64_t freq[SYMBOLS]) {
    char magic[4];
    fread(magic, 1, 4, f);

    if (strncmp(magic, "HUF1", 4) != 0) {
        printf("Invalid file type.\n");
        exit(1);
    }

    fread(file_count, sizeof(uint32_t), 1, f);

    for (int i = 0; i < *file_count; i++) {
        uint16_t len;

        fread(&len, sizeof(uint16_t), 1, f);

        fread(files[i].path, 1, len, f);

        files[i].path[len] = 0;

        fread(&files[i].size, sizeof(uint64_t), 1, f);
    }

    fread(freq, sizeof(uint64_t), SYMBOLS, f);
}

void decode_files(BitReader *br, Node *root, FILE *out, uint64_t bytes) {
    Node *current = root;
    uint64_t written = 0;

    while (written < bytes) {
        int bit = br_read_bit(br);

        if (bit == 0) {
            current = current->left;
        } else {
            current = current->right;
        }

        if (!current->left && !current->right) {
            fputc(current->symbol, out);
            current = root;
            written++;
        }
    }
}

void create_directory(const char *path) {
    char tmp[512];
    strcpy(tmp, path);

    char *p = strrchr(tmp, '/');

    if (!p)
        return;

    *p = '\0';
    mkdir(tmp, 0755);
}

void extract_files(BitReader *br, Node *root, FileEntry files[],
                   int file_count) {
    for (int i = 0; i < file_count; i++) {
        create_directory(files[i].path);
        FILE *out = fopen(files[i].path, "wb");

        decode_files(br, root, out, files[i].size);

        fclose(out);
    }
}

void decompress_files(const char *file) {
    FILE *f = fopen(file, "rb");

    FileEntry files[MAX_FILES];
    int file_count;
    uint64_t freq[SYMBOLS];

    read_header(f, files, &file_count, freq);

    Node *root = build_huffman(freq);

    BitReader br;
    br_init(&br, f);

    extract_files(&br, root, files, file_count);

    fclose(f);
}

int main(int argc, char **argv) {
    if (argc < 2) {
        printf("usage: decompressor <file>\n");
        return 1;
    }

    decompress_files(argv[1]);

    return 0;
}
