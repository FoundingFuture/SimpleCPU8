
#include "basic.h"

/* A string on the heap is a length byte and then its bytes. An offset of 0
 * means the empty string, which is why the heap starts at 1: zero has to
 * mean nothing at all.
 */
unsigned char heap[HEAPMAX];
unsigned int heap_top;
unsigned int svar[NSTRS];

/* Where a temporary lives while an expression is being worked out. The
 * collector has to see these too, or a concatenation in the middle of a
 * PRINT would be swept out from under it.
 */
#define NTEMP 8
unsigned int temps[NTEMP];
unsigned char ntemp;

void str_init(void)
{
    unsigned char i;
    heap_top = 1;
    ntemp = 0;
    for (i = 0; i < NSTRS; i++) svar[i] = 0;
}

static void keep(unsigned int s)
{
    if (s == 0) return;
    if (ntemp >= NTEMP) ntemp = 0;   /* one statement never needs more */
    temps[ntemp] = s;
    ntemp = ntemp + 1;
}

unsigned int str_new(unsigned int len)
{
    unsigned int at;
    if (len > 255) { rt_error(E_RANGE); return 0; }
    if (heap_top + len + 1 >= HEAPMAX) {
        str_collect();
        if (heap_top + len + 1 >= HEAPMAX) { rt_error(E_MEMORY); return 0; }
    }
    at = heap_top;
    heap[at] = len;
    heap_top = heap_top + len + 1;
    keep(at);
    return at;
}

unsigned char str_len(unsigned int s)
{
    if (s == 0) return 0;
    return heap[s];
}

unsigned char str_at(unsigned int s, unsigned int i)
{
    if (s == 0) return 0;
    if (i >= heap[s]) return 0;
    return heap[s + 1 + i];
}

unsigned int str_from(char *t)
{
    unsigned int n;
    unsigned int s;
    unsigned int i;
    n = 0;
    while (t[n]) n = n + 1;
    s = str_new(n);
    if (s == 0) return 0;
    for (i = 0; i < n; i++) heap[s + 1 + i] = t[i];
    return s;
}

unsigned int str_cat(unsigned int a, unsigned int b)
{
    unsigned int la;
    unsigned int lb;
    unsigned int s;
    unsigned int i;

    la = str_len(a);
    lb = str_len(b);
    if (la + lb > 255) { rt_error(E_RANGE); return 0; }
    s = str_new(la + lb);
    if (s == 0) return 0;
    for (i = 0; i < la; i++) heap[s + 1 + i] = heap[a + 1 + i];
    for (i = 0; i < lb; i++) heap[s + 1 + la + i] = heap[b + 1 + i];
    return s;
}

void str_put(unsigned int s)
{
    unsigned int i;
    unsigned int n;
    n = str_len(s);
    for (i = 0; i < n; i++) term_putc(heap[s + 1 + i]);
}

int str_cmp(unsigned int a, unsigned int b)
{
    unsigned int i;
    unsigned int la;
    unsigned int lb;
    unsigned char ca;
    unsigned char cb;

    la = str_len(a);
    lb = str_len(b);
    i = 0;
    while (i < la && i < lb) {
        ca = heap[a + 1 + i];
        cb = heap[b + 1 + i];
        if (ca < cb) return -1;
        if (ca > cb) return 1;
        i = i + 1;
    }
    if (la < lb) return -1;
    if (la > lb) return 1;
    return 0;
}

/* The collector. Compacting, and it moves every live string down over the
 * dead ones. A string is live when a variable or a live temporary names it.
 *
 * DESIGN: this is why the machine gained CMD_RAM_MOVE. Sliding a heap down
 * over itself is a move of overlapping blocks, and without the command it
 * was a load and a store per byte in a CPU loop.
 */
void str_collect(void)
{
    unsigned int at;
    unsigned int len;
    unsigned int dst;
    unsigned char i;
    unsigned char live;

    dst = 1;
    at = 1;
    while (at < heap_top) {
        len = heap[at];
        live = 0;
        for (i = 0; i < NSTRS; i++) if (svar[i] == at) live = 1;
        for (i = 0; i < ntemp; i++) if (temps[i] == at) live = 1;
        if (live) {
            if (dst != at) {
                memmove(&heap[dst], &heap[at], len + 1);
                for (i = 0; i < NSTRS; i++) if (svar[i] == at) svar[i] = dst;
                for (i = 0; i < ntemp; i++) if (temps[i] == at) temps[i] = dst;
            }
            dst = dst + len + 1;
        }
        at = at + len + 1;
    }
    heap_top = dst;
}
