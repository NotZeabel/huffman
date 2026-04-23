#include "utils/bitreader.c"
#include "utils/heap.c"
#include <libgen.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>

// ===== DEFINITIONS =====
#define BUFFER_SIZE 128 * 1024
#define SYMBOLS 256
#define MAX_FILES 1024

typedef struct {
    char path[512];
    uint64_t size;
} FileEntry;

typedef struct {
    char *buffer;
    char *path;
    uint64_t size;
} thread_args;

// Mutex variables
pthread_mutex_t first_lock;
pthread_t *tArray;

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

void decode_files(BitReader *br, Node *root, uint64_t bytes,
                  thread_args *new_thread) {
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
            new_thread->buffer[written] = current->symbol;
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

void *write_file(void *arg) {
    thread_args *data_arg = arg;

    create_directory(data_arg->path);
    FILE *out = fopen(data_arg->path, "wb");

    fwrite(data_arg->buffer, 1, data_arg->size, out);

    fclose(out);

    free(data_arg->path);
    free(data_arg->buffer);
    free(arg);
}

void extract_files(BitReader *br, Node *root, FileEntry files[],
                   int file_count) {
    for (int i = 0; i < file_count; i++) {

        thread_args *new_arg = malloc(sizeof(thread_args));
        new_arg->buffer = malloc(sizeof(char) * files[i].size);
        new_arg->path = strdup(files[i].path);
        new_arg->size = files[i].size;

        decode_files(br, root, files[i].size, new_arg);

        int status =
            pthread_create(&tArray[i], NULL, write_file, (void *)new_arg);
        if (status != 0) {
            free(new_arg->path);
            free(new_arg->buffer);
            free(new_arg);
        }
    }
    for (int i = 0; i < file_count; i++) {
        pthread_join(tArray[i], NULL);
    }
}

void init_global(int file_count) {
    pthread_mutex_init(&first_lock, NULL);
    tArray = malloc(sizeof(pthread_t) * file_count);
}

void decompress_files(const char *file) {
    FILE *f = fopen(file, "rb");

    FileEntry files[MAX_FILES];
    int file_count;
    uint64_t freq[SYMBOLS];

    read_header(f, files, &file_count, freq);

    mkdir("Recuperados", 0755);
    mkdir("Recuperados/threads", 0755);

    for (int i = 0; i < file_count; i++) {
        char *solo_nombre = basename(files[i].path);

        char nueva_ruta[1024];
        snprintf(nueva_ruta, sizeof(nueva_ruta), "Recuperados/threads/%s",
                 solo_nombre);

        strncpy(files[i].path, nueva_ruta, sizeof(files[i].path) - 1);
        files[i].path[sizeof(files[i].path) - 1] = '\0';
    }

    // Initialize global variables for threads
    init_global(file_count);

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
    struct timeval starting_time, ending_time;

    gettimeofday(&starting_time, 0);
    decompress_files(argv[1]);
    gettimeofday(&ending_time, 0);

    int seconds = ending_time.tv_sec - starting_time.tv_sec;
    int microseconds = ending_time.tv_usec - starting_time.tv_usec;
    double final_time = seconds + microseconds * 1e-6;

    printf("Descompresión finalizada. Archivos en: ./Recuperados/threads/\n");
    printf("Finalizó correctamente en %f segundos\n", final_time);

    free(tArray);

    return 0;
}
