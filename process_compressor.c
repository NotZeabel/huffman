#include "utils/bitwriter.c"
#include "utils/heap.c"
#include <dirent.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <unistd.h>

// DEFINICIONES
#define SYMBOLS 256
#define MAX_FILES 1024

typedef struct {
    char path[512];
    uint64_t size;
} FileEntry;

// Variables globales
FileEntry files[MAX_FILES];
int file_count = 0;
uint64_t freq[SYMBOLS] = {0};
char *codes[SYMBOLS];

// LÓGICA DE HUFFMAN

Node *build_huffman() {
    for (int i = 0; i < SYMBOLS; i++) {
        if (freq[i] == 0)
            continue;
        Node *n = malloc(sizeof(Node));
        n->symbol = i;
        n->freq = freq[i];
        n->left = n->right = NULL;
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

// MANEJO DE ARCHIVOS Y HEADER

void scan_dir(const char *dir_path) {
    DIR *dir = opendir(dir_path);
    if (!dir)
        return;
    struct dirent *entry;

    while ((entry = readdir(dir)) != NULL && file_count < MAX_FILES) {
        if (entry->d_type == DT_REG) {
            char fullpath[256];
            snprintf(fullpath, sizeof(fullpath), "%s%s", dir_path,
                     entry->d_name);

            FILE *f = fopen(fullpath, "rb");
            if (!f)
                continue;

            fseek(f, 0, SEEK_END);
            files[file_count].size = ftell(f);
            strcpy(files[file_count].path, fullpath);
            file_count++;
            fclose(f);
        }
    }
    closedir(dir);
}

void write_header(FILE *out) {
    fwrite("HUF1", 1, 4, out);
    fwrite(&file_count, sizeof(uint32_t), 1, out);
    for (int i = 0; i < file_count; i++) {
        uint16_t len = strlen(files[i].path);
        fwrite(&len, sizeof(uint16_t), 1, out);
        fwrite(files[i].path, 1, len, out);
        fwrite(&files[i].size, sizeof(uint64_t), 1, out);
    }
    fwrite(freq, sizeof(uint64_t), SYMBOLS, out);
}

// VERSIÓN FORK (PROCESOS)

void compress_fork(const char *input_dir, const char *output, int num_procs) {
    scan_dir(input_dir);

    // 1. Crear memoria compartida para las frecuencias globales
    uint64_t *shared_freq =
        mmap(NULL, sizeof(uint64_t) * SYMBOLS, PROT_READ | PROT_WRITE,
             MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    memset(shared_freq, 0, sizeof(uint64_t) * SYMBOLS);

    int files_per_proc = (file_count + num_procs - 1) / num_procs;

    for (int i = 0; i < num_procs; i++) {
        pid_t pid = fork();
        if (pid == 0) { // Proceso Hijo
            int start = i * files_per_proc;
            int end = (start + files_per_proc > file_count)
                          ? file_count
                          : start + files_per_proc;

            for (int j = start; j < end; j++) {
                FILE *f = fopen(files[j].path, "rb");
                if (!f)
                    continue;
                int c;
                while ((c = fgetc(f)) != EOF) {
                    // Incremento atómico en memoria compartida
                    __sync_fetch_and_add(&shared_freq[(unsigned char)c], 1);
                }
                fclose(f);
            }
            exit(0);
        }
    }

    // El padre espera a todos los hijos
    for (int i = 0; i < num_procs; i++)
        wait(NULL);

    // Copiar resultados de memoria compartida a la variable global freq
    memcpy(freq, shared_freq, sizeof(uint64_t) * SYMBOLS);
    munmap(shared_freq, sizeof(uint64_t) * SYMBOLS);

    // Construcción serial del árbol (Solo el padre)
    Node *root = build_huffman();
    char code_buffer[256];
    build_codes(root, code_buffer, 0);

    // Escritura del archivo comprimido
    FILE *out = fopen(output, "wb");
    write_header(out);
    BitWriter bw;
    bw_init(&bw, out);

    // Codificación
    for (int i = 0; i < file_count; i++) {
        FILE *f = fopen(files[i].path, "rb");
        int c;
        while ((c = fgetc(f)) != EOF) {
            bw_write_code(&bw, codes[(unsigned char)c]);
        }
        fclose(f);
    }

    bw_flush(&bw);
    fclose(out);
}

int main(int argc, char **argv) {
    if (argc < 3) {
        printf("Uso: %s <dir> <output> [n_procesos]\n", argv[0]);
        return 1;
    }

    int n_procs = (argc == 4) ? atoi(argv[3]) : 4; // Por defecto 4 procesos

    struct timeval start, end;
    gettimeofday(&start, NULL);

    compress_fork(argv[1], argv[2], n_procs);

    gettimeofday(&end, NULL);
    long seconds = end.tv_sec - start.tv_sec;
    long useconds = end.tv_usec - start.tv_usec;
    long mtime = ((seconds) * 1000 + useconds / 1000.0) + 0.5;

    printf("Tiempo de ejecución (fork): %ld ms\n", mtime);

    return 0;
}
