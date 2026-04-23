#include "utils/bitwriter.c"
#include "utils/heap.c"
#include <dirent.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/time.h>

// ===== DEFINITIONS =====

#define SYMBOLS 256
#define MAX_FILES 1024

typedef struct {
    char path[512];
    uint64_t size;
} FileEntry;

typedef struct {
    char *fullpath;
    int file_number;
} thread_args;

FileEntry files[MAX_FILES];
int file_count = 0;
uint64_t freq[SYMBOLS] = {0};
char *codes[SYMBOLS];

// Mutex variables
pthread_mutex_t first_lock;
pthread_t *tArray;

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
    fwrite(&file_count, sizeof(uint32_t), 1, out);

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

void merge_freq(uint64_t input_freq[SYMBOLS]) {
    pthread_mutex_lock(&first_lock);
    for (int i = 0; i < SYMBOLS; i++)
        freq[i] += input_freq[i];
    pthread_mutex_unlock(&first_lock);
}

void *read_file(void *arg) {
    thread_args *data_arg = arg;
    char *fullpath = data_arg->fullpath;
    int thread_id = data_arg->file_number;

    FILE *f = fopen(fullpath, "rb");
    if (!f)
        pthread_exit((void *)3);

    strcpy(files[thread_id].path, fullpath);

    uint64_t size = 0;
    uint64_t local_freq[SYMBOLS] = {0};
    int c;

    while ((c = fgetc(f)) != EOF) {
        local_freq[(unsigned char)c]++;
        size++;
    }

    files[thread_id].size = size;

    merge_freq(local_freq);

    fclose(f);

    free(fullpath);
    free(arg);
}

void scan_dir(const char *dir_path) {
    DIR *dir = opendir(dir_path);
    struct dirent *entry;

    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type == DT_REG) {
            char fullpath[256];
            snprintf(fullpath, sizeof(fullpath), "%s%s", dir_path,
                     entry->d_name);

            int thread_id = file_count++;

            thread_args *new_arg = malloc(sizeof(thread_args));
            new_arg->fullpath = strdup(fullpath);
            new_arg->file_number = thread_id;

            int status = pthread_create(&tArray[thread_id], NULL, read_file,
                                        (void *)new_arg);
            if (status != 0) {
                free(new_arg->fullpath);
                free(new_arg);
                file_count--;
            }
        }
    }

    for (int i = 0; i < file_count; i++) {
        pthread_join(tArray[i], NULL);
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

void init_global() {
    pthread_mutex_init(&first_lock, NULL);
    tArray = malloc(sizeof(pthread_t) * MAX_FILES);
}

int main(int argc, char **argv) {
    if (argc != 3) {
        printf("usage: compressor <dir> <output>\n");
        return 1;
    }

    struct timeval starting_time, ending_time;
    init_global();

    gettimeofday(&starting_time, 0);
    compress_directory(argv[1], argv[2]);
    gettimeofday(&ending_time, 0);

    int seconds = ending_time.tv_sec - starting_time.tv_sec;
    int microseconds = ending_time.tv_usec - starting_time.tv_usec;
    double final_time = seconds + microseconds * 1e-6;

    printf("Finalizó correctamente en %f segundos\n", final_time);

    free(tArray);

    return 0;
}
