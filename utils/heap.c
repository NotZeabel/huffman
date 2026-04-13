#include <stdint.h>
#include <string.h>

typedef struct Node {
    unsigned char symbol;
    uint64_t freq;
    struct Node *left;
    struct Node *right;
} Node;

Node *heap[512];
int heap_size = 0;

void swap(int a, int b) {
    Node *t = heap[a];
    heap[a] = heap[b];
    heap[b] = t;
}

void heap_push(Node *n) {
    int i = heap_size++;
    heap[i] = n;

    while (i && heap[i]->freq < heap[(i - 1) / 2]->freq) {
        swap(i, (i - 1) / 2);
        i = (i - 1) / 2;
    }
}

Node *heap_pop() {
    if (heap_size == 0)
        return NULL;

    Node *root = heap[0];
    heap[0] = heap[--heap_size];

    int i = 0;

    while (1) {
        int l = 2 * i + 1;
        int r = 2 * i + 2;
        int smallest = i;

        if (l < heap_size && heap[l]->freq < heap[smallest]->freq)
            smallest = l;

        if (r < heap_size && heap[r]->freq < heap[smallest]->freq)
            smallest = r;

        if (smallest == i)
            break;

        swap(i, smallest);
        i = smallest;
    }

    return root;
}
