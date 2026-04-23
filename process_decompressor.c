#include "utils/bitreader.c"
#include "utils/heap.c"
#include <libgen.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <unistd.h>

// DEFINITIONS
#define SYMBOLS 256
#define MAX_FILES 1024

typedef struct {
    char path[512];
    uint64_t size;
} FileEntry;

// LÓGICA DE HUFFMAN
Node *build_huffman(uint64_t freq[SYMBOLS]) {
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

void read_header(FILE *f, FileEntry files[], int *file_count,
                 uint64_t freq[SYMBOLS]) {
    char magic[4];
    fread(magic, 1, 4, f);
    if (strncmp(magic, "HUF1", 4) != 0) {
        printf("Error: Archivo inválido.\n");
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
        if (bit == 0)
            current = current->left;
        else
            current = current->right;

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

// VERSIÓN FORK
void extract_files_fork(const char *bin_path, Node *root, FileEntry files[],
                        int file_count, int n_procs) {
    // Para simplificar la concurrencia en lectura de bits sin pre-calcular
    // offsets, los hijos procesarán bloques de archivos.
    int files_per_proc = (file_count + n_procs - 1) / n_procs;

    for (int p = 0; p < n_procs; p++) {
        pid_t pid = fork();
        if (pid == 0) { // Proceso Hijo
            // Cada hijo abre su propio descriptor de archivo para no compartir
            // el puntero de lectura
            FILE *f_hijo = fopen(bin_path, "rb");
            FileEntry dummy_files[MAX_FILES];
            uint64_t dummy_freq[SYMBOLS];
            int total_f;

            // Reposicionar el BitReader al inicio de los datos
            read_header(f_hijo, dummy_files, &total_f, dummy_freq);
            BitReader br;
            br_init(&br, f_hijo);

            int start = p * files_per_proc;
            int end = (start + files_per_proc > file_count)
                          ? file_count
                          : start + files_per_proc;

            // IMPORTANTE: El hijo debe saltar los bits de los archivos que no
            // le tocan En Huffman, esto requiere decodificar "en el aire" sin
            // escribir
            for (int i = 0; i < file_count; i++) {
                if (i >= start && i < end) {
                    create_directory(files[i].path);
                    FILE *out = fopen(files[i].path, "wb");
                    decode_files(&br, root, out, files[i].size);
                    fclose(out);
                } else if (i < start) {
                    // "Decodificación fantasma" para mover el BitReader a la
                    // posición correcta
                    uint64_t skipped = 0;
                    Node *curr = root;
                    while (skipped < files[i].size) {
                        int bit = br_read_bit(&br);
                        curr = (bit == 0) ? curr->left : curr->right;
                        if (!curr->left && !curr->right) {
                            curr = root;
                            skipped++;
                        }
                    }
                } else {
                    break; // Ya terminó su bloque
                }
            }
            fclose(f_hijo);
            exit(0);
        }
    }

    for (int i = 0; i < n_procs; i++)
        wait(NULL);
}

int main(int argc, char **argv) {
    if (argc < 2) {
        printf("Uso: %s <archivo.huf> [n_procesos]\n", argv[0]);
        return 1;
    }

    int n_procs = (argc == 3) ? atoi(argv[2]) : 4;
    struct timeval start_t, end_t;
    gettimeofday(&start_t, 0);

    FILE *f = fopen(argv[1], "rb");
    if (!f) {
        printf("Error al abrir el archivo comprimido.\n");
        return 1;
    }

    FileEntry files[MAX_FILES];
    int file_count;
    uint64_t freq[SYMBOLS];
    read_header(f, files, &file_count, freq);
    fclose(f);

    // --- LÓGICA DE RUTAS RELATIVAS ---

    // 1. Crear la estructura de carpetas: ./Recuperados/libros/
    // Primero la base, luego la subcarpeta
    mkdir("Recuperados", 0755);
    mkdir("Recuperados/procesos", 0755);

    for (int i = 0; i < file_count; i++) {
        // Extraemos solo el nombre del archivo (ej: quijote.txt)
        // Ignoramos cualquier ruta que traiga el .huf (sea absoluta o relativa)
        char *solo_nombre = basename(files[i].path);

        char nueva_ruta[1024];
        // Forzamos la ruta a ./Recuperados/libros/nombre_archivo.txt
        snprintf(nueva_ruta, sizeof(nueva_ruta), "Recuperados/procesos/%s",
                 solo_nombre);

        strncpy(files[i].path, nueva_ruta, sizeof(files[i].path) - 1);
        files[i].path[sizeof(files[i].path) - 1] = '\0';
    }

    Node *root = build_huffman(freq);
    extract_files_fork(argv[1], root, files, file_count, n_procs);

    gettimeofday(&end_t, 0);
    double total_time = (end_t.tv_sec - start_t.tv_sec) +
                        (end_t.tv_usec - start_t.tv_usec) * 1e-6;

    printf("Descompresión finalizada. Archivos en: ./Recuperados/procesos/\n");
    printf("Tiempo total: %f segundos\n", total_time);

    return 0;
}
