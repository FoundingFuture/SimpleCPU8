#include "basic.h"

/* The storage driver: LOAD, SAVE, DELETE and CATALOG over the cartridge's
 * named slots, through the storage device. A slot holds the text LIST
 * prints. The IDE's editor and this driver read the same thing.
 */

/* The text goes through one block, larger than PROGMAX. A listing spends
 * five digits and a space per line where a record spends three bytes.
 */
static char textbuf[TEXTMAX];

/* One byte past the device's limit. A long name then reaches the device
 * and is refused there rather than cut to fit.
 */
static char name_buf[18];

/* The quoted name after the command word. Reads the device's answer into
 * an error, so every command reports the same way.
 */
static unsigned char take_name(void)
{
    unsigned char n;
    unsigned char i;
    if (lx_tok != T_STR) { rt_error(E_SYNTAX); return 0; }
    n = str_len(lx_str);
    if (n > 17) n = 17;
    for (i = 0; i < n; i++) name_buf[i] = heap[lx_str + 1 + i];
    name_buf[n] = 0;
    return 1;
}

static unsigned char check(void)
{
    unsigned char s;
    s = sto_status();
    if (s == STO_OK) return 1;
    if (s == STO_NOT_FOUND) rt_error(E_NOTFOUND);
    else if (s == STO_FULL) rt_error(E_STOFULL);
    else if (s == STO_BAD_NAME) rt_error(E_BADNAME);
    else rt_error(E_SYNTAX);
    return 0;
}

/* The listing, as ed_list prints it, into the block. */
static unsigned char serialise(void)
{
    unsigned int p;
    unsigned int at;
    unsigned int n;
    unsigned int i;
    at = 0;
    p = 0;
    while (prog[p] || prog[p + 1]) {
        unsigned char digits[6];
        unsigned char d;
        n = (prog[p] << 8) | prog[p + 1];
        d = 0;
        while (n) { digits[d] = 48 + (n % 10); n = n / 10; d = d + 1; }
        if (at + d + prog[p + 2] + 2 >= TEXTMAX) { rt_error(E_MEMORY); return 0; }
        while (d) { d = d - 1; textbuf[at] = digits[d]; at = at + 1; }
        textbuf[at] = 32;
        at = at + 1;
        i = 0;
        while (prog[p + 3 + i]) { textbuf[at] = prog[p + 3 + i]; at = at + 1; i = i + 1; }
        textbuf[at] = 10;
        at = at + 1;
        p = p + prog[p + 2];
    }
    textbuf[at] = 0;
    return 1;
}

/* Every numbered line goes into the store the way a typed one does. A line
 * with no number is skipped. A saved listing has none, and running one
 * while loading would be a surprise.
 */
static void enter_lines(void)
{
    unsigned int at;
    unsigned int start;
    at = 0;
    while (textbuf[at]) {
        start = at;
        while (textbuf[at] && textbuf[at] != 10) at = at + 1;
        if (textbuf[at] == 10) { textbuf[at] = 0; at = at + 1; }
        if (at > start + 1 && textbuf[at - 2] == 13) textbuf[at - 2] = 0;
        {
            unsigned int i;
            int n;
            i = start;
            while (textbuf[i] == 32) i = i + 1;
            if (textbuf[i] < 48 || textbuf[i] > 57) continue;
            n = 0;
            while (textbuf[i] >= 48 && textbuf[i] <= 57) {
                n = n * 10 + (textbuf[i] - 48);
                i = i + 1;
            }
            while (textbuf[i] == 32) i = i + 1;
            ed_store(n, &textbuf[i]);
            if (err) return;
        }
    }
}

static void do_load(void)
{
    sto_load(textbuf, name_buf, TEXTMAX);
    if (!check()) return;
    /* The old program is gone, and with it every line a running one could
     * return to. So a program that loads another stops here.
     */
    ed_new();
    running = 0;
    enter_lines();
}

static void do_save(void)
{
    if (!serialise()) return;
    sto_save(textbuf, name_buf);
    check();
}

static void do_catalog(void)
{
    sto_catalog(textbuf, TEXTMAX);
    if (!check()) return;
    term_puts(textbuf);
}

/* Returns 1 when the text was one of the four commands, whether or not it
 * went well. The chain stops at the first link that knows the word.
 */
unsigned char sto_bang(char *text)
{
    lx_start(text);
    if (lx_is("CATALOG")) { do_catalog(); return 1; }
    if (lx_is("LOAD")) { lx_next(); if (take_name()) do_load(); return 1; }
    if (lx_is("SAVE")) { lx_next(); if (take_name()) do_save(); return 1; }
    if (lx_is("DELETE")) {
        lx_next();
        if (take_name()) { sto_delete(name_buf); check(); }
        return 1;
    }
    return 0;
}
