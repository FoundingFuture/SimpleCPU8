
#include "basic.h"

/* A line is two bytes of number, one of record length, then the text. A line
 * number of zero ends the program, and BASIC has no line zero, so nothing is
 * lost by using it as the terminator.
 */
unsigned char prog[PROGMAX];
unsigned int prog_len;

void ed_new(void)
{
    prog[0] = 0;
    prog[1] = 0;
    prog[2] = 3;
    prog_len = 3;
}

static unsigned int lineno_at(unsigned int p)
{
    return (prog[p] << 8) | prog[p + 1];
}

/* Where a line is, or where it would go. Lines are kept in order, so RUN and
 * LIST are a walk and GOTO is a search that can stop early.
 */
unsigned int ed_find(int line)
{
    unsigned int p;
    p = 0;
    while (lineno_at(p)) {
        if (lineno_at(p) >= line) return p;
        p = p + prog[p + 2];
    }
    return p;
}

static void remove_at(unsigned int p)
{
    unsigned int len;
    len = prog[p + 2];
    memmove(&prog[p], &prog[p + len], prog_len - p - len);
    prog_len = prog_len - len;
}

void ed_store(int line, char *text)
{
    unsigned int p;
    unsigned int n;
    unsigned int rec;
    unsigned int i;

    p = ed_find(line);
    if (lineno_at(p) == line) remove_at(p);

    n = 0;
    while (text[n]) n = n + 1;
    /* An empty line deletes: typing the number alone rubs the line out, the
     * way every BASIC has worked.
     */
    if (n == 0) return;
    if (n > 250) { rt_error(E_RANGE); return; }
    rec = n + 4;
    if (prog_len + rec >= PROGMAX) { rt_error(E_MEMORY); return; }

    memmove(&prog[p + rec], &prog[p], prog_len - p);
    prog[p] = line >> 8;
    prog[p + 1] = line;
    prog[p + 2] = rec;
    for (i = 0; i < n; i++) prog[p + 3 + i] = text[i];
    prog[p + 3 + n] = 0;
    prog_len = prog_len + rec;
}

void ed_list(void)
{
    unsigned int p;
    unsigned int i;
    p = 0;
    while (lineno_at(p)) {
        term_putn(lineno_at(p));
        term_putc(32);
        i = 0;
        while (prog[p + 3 + i]) { term_putc(prog[p + 3 + i]); i = i + 1; }
        term_nl();
        p = p + prog[p + 2];
    }
}
