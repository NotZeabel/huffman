#include "utils/bitwriter.c"
#include "utils/heap.c"
#include <dirent.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SYMBOLS 256
#define MAX_FILES 1024

// ===== DEFINITIONS =====

typedef struct {
    char path[512];
    uint64_t size;
} FileEntry;

FileEntry files[MAX_FILES];
int file_count = 0;
uint64_t freq[SYMBOLS] = {0};
char *codes[SYMBOLS];

// ===== HUFFMAN MIEDO =====

Node *build_huffman() {
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

void build_codes(Node *node, char *prefix, int length) {
    if (!node->left && !node->right) {
        prefix[length] = '\0';
        codes[node->symbol] = strdup(prefix);
        return;
    }

    prefix[length] = '0';
    build_codes(node->left, prefix, length + 1);

    prefix[length] = '1';
    build_codes(node->right, prefix, length + 1);
}

// ===== FILE LOGIC =====

void write_header(FILE *out) {
    fwrite("HUF1", 1, 4, out);
    fwrite(&file_count, 1, 1, out);

    for (int i = 0; i < file_count; i++) {
        uint16_t len = strlen(files[i].path);

        fwrite(&len, sizeof(uint16_t), 1, out);
        fwrite(files[i].path, 1, len, out);
        fwrite(&files[i].size, sizeof(uint64_t), 1, out);
    }

    fwrite(freq, sizeof(uint64_t), SYMBOLS, out);
}

void encode_files(BitWriter *bw) {
    for (int i = 0; i < file_count; i++) {
        FILE *f = fopen(files[i].path, "rb");
        int c;

        while ((c = fgetc(f)) != EOF) {
            bw_write_code(bw, codes[(unsigned char)c]);
        }

        fclose(f);
    }
}

void scan_dir(const char *dir_path) {
    DIR *dir = opendir(dir_path);
    struct dirent *entry;

    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type == DT_REG) {
            char fullpath[256];
            snprintf(fullpath, sizeof(fullpath), "%s%s", dir_path,
                     entry->d_name);

            FILE *f = fopen(fullpath, "rb");
            if (!f)
                continue;

            strcpy(files[file_count].path, fullpath);

            uint64_t size = 0;
            int c;

            while ((c = fgetc(f)) != EOF) {
                freq[(unsigned char)c]++;
                size++;
            }

            files[file_count].size = size;
            file_count++;

            fclose(f);
        }
    }

    closedir(dir);
}

void compress_directory(const char *input_dir, const char *output) {
    scan_dir(input_dir);

    Node *root = build_huffman();

    char code_buffer[256];
    build_codes(root, code_buffer, 0);

    FILE *out = fopen(output, "wb");

    write_header(out);

    BitWriter bw;
    bw_init(&bw, out);

    encode_files(&bw);

    bw_flush(&bw);

    fclose(out);
}

int main(int argc, char **argv) {
    if (argc != 3) {
        printf("usage: compressor <dir> <output>\n");
        return 1;
    }

    compress_directory(argv[1], argv[2]);

    return 0;
}
