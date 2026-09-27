
#include "basic.h"
#include "keywords.h"

/* A line is two bytes of number, one of record length, then the text. A line
 * number of zero ends the program, and BASIC has no line zero, so nothing is
 * lost by using it as the terminator.
 */
unsigned char prog[PROGMAX];

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

static void keywords_up(unsigned char *t);

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
    if (n > 250) { rt_error(E_LINELONG); return; }
    rec = n + 4;
    if (prog_len + rec >= PROGMAX) { rt_error(E_MEMORY); return; }

    memmove(&prog[p + rec], &prog[p], prog_len - p);
    prog[p] = line >> 8;
    prog[p + 1] = line;
    prog[p + 2] = rec;
    for (i = 0; i < n; i++) prog[p + 3 + i] = text[i];
    prog[p + 3 + n] = 0;
    keywords_up(&prog[p + 3]);
    prog_len = prog_len + rec;
}

/* LIST shows the lines numbered from first to last, both included. The
 * walk starts at the first line not below first and stops at the first
 * line past last, since lines are in order.
 */
void ed_list(unsigned int first, unsigned int last)
{
    unsigned int p;
    unsigned int i;
    p = ed_find(first);
    while (lineno_at(p) && lineno_at(p) <= last) {
        term_putn(lineno_at(p));
        term_putc(32);
        i = 0;
        while (prog[p + 3 + i]) { term_putc(prog[p + 3 + i]); i = i + 1; }
        term_nl();
        p = p + prog[p + 2];
    }
}

/* ---- RENUM ---- */

static char renum_buf[256];

static unsigned char upper_of(unsigned char c)
{
    if (c >= 97 && c <= 122) return c - 32;
    return c;
}

static unsigned char is_letter(unsigned char c)
{
    c = upper_of(c);
    return c >= 65 && c <= 90;
}

static unsigned char is_digit(unsigned char c) { return c >= 48 && c <= 57; }

/* True when the n characters at w spell kw, case ignored. */
static unsigned char word_is(unsigned char *w, unsigned int n, char *kw)
{
    unsigned int i;
    i = 0;
    while (i < n) {
        if (kw[i] == 0 || upper_of(w[i]) != kw[i]) return 0;
        i = i + 1;
    }
    return kw[n] == 0;
}

/* ---- Reserved words in capitals ---- */

static unsigned char is_hex(unsigned char c)
{
    c = upper_of(c);
    return is_digit(c) || (c >= 65 && c <= 70);
}

/* A letter, a digit or an underscore: what a name goes on with. */
static unsigned char is_name(unsigned char c)
{
    return is_letter(c) || is_digit(c) || c == 95;
}

/* True when the n characters at w spell a word of BASIC_KEYWORDS, case
 * ignored. The string holds each word between two spaces.
 */
static unsigned char is_keyword(unsigned char *w, unsigned int n)
{
    char *k;
    unsigned int i;
    k = BASIC_KEYWORDS;
    while (k[1]) {
        k = k + 1;
        i = 0;
        while (i < n && k[i] == upper_of(w[i])) i = i + 1;
        if (i == n && k[n] == 32) return 1;
        while (*k != 32) k = k + 1;
    }
    return 0;
}

/* The reserved words of a stored line in capitals, so a line typed in
 * small letters is kept the way the IDE sends one. canonicalLine in
 * program.cpp is the IDE's copy of the rule. Strings, the rest of a line
 * after REM or a bang, names, numbers and spacing stay as typed.
 */
static void keywords_up(unsigned char *t)
{
    unsigned int i;
    unsigned int j;
    unsigned int k;
    unsigned char rem;
    i = 0;
    while (t[i]) {
        if (t[i] == 34) {
            i = i + 1;
            while (t[i] && t[i] != 34) i = i + 1;
            if (t[i]) i = i + 1;
        } else if (t[i] == 36 && is_hex(t[i + 1])) {
            i = i + 1;
            while (is_hex(t[i])) i = i + 1;
        } else if (is_digit(t[i])) {
            /* A number runs over letters, as the IDE's does: 1TO stays. */
            while (is_name(t[i]) || t[i] == 46) i = i + 1;
        } else if (is_letter(t[i]) || t[i] == 95) {
            j = i;
            while (is_name(t[j])) j = j + 1;
            if (t[j] == 36) j = j + 1;
            if (is_keyword(&t[i], j - i)) {
                rem = word_is(&t[i], j - i, "REM");
                for (k = i; k < j; k++) t[k] = upper_of(t[k]);
                if (rem) return;
            }
            i = j;
        } else if (t[i] == 33) {
            return;
        } else {
            i = i + 1;
        }
    }
}

/* The number line `old` gets, or 0 when there is no such line. */
static unsigned int renum_of(unsigned int old, unsigned int start, unsigned int step)
{
    unsigned int p;
    unsigned int n;
    p = 0;
    n = start;
    while (lineno_at(p)) {
        if (lineno_at(p) == old) return n;
        n = n + step;
        p = p + prog[p + 2];
    }
    return 0;
}

/* The text of the line at p with its references renumbered, into
 * renum_buf. Returns its length, or 251 when it would be too long. Strings
 * are copied as they are, and REM and ! end the scan, since the rest of
 * their line is not BASIC.
 */
static unsigned int renum_text(unsigned int p, unsigned int start, unsigned int step)
{
    unsigned char *src;
    unsigned int i;
    unsigned int o;
    unsigned int j;
    unsigned int num;
    unsigned int nn;
    unsigned int d;
    unsigned char c;
    unsigned char digits[6];
    unsigned char nd;
    unsigned char big;
    src = &prog[p + 3];
    i = 0;
    o = 0;
    while (src[i]) {
        if (o > 250) return 251;
        c = src[i];
        if (c == 34) {
            renum_buf[o] = c; o = o + 1; i = i + 1;
            while (src[i] && src[i] != 34 && o <= 250) { renum_buf[o] = src[i]; o = o + 1; i = i + 1; }
            if (src[i] == 34) { renum_buf[o] = 34; o = o + 1; i = i + 1; }
        } else if (c == 33) {
            while (src[i] && o <= 250) { renum_buf[o] = src[i]; o = o + 1; i = i + 1; }
        } else if (is_letter(c)) {
            j = i;
            while (is_letter(src[j]) || is_digit(src[j]) || src[j] == 36) j = j + 1;
            if (word_is(&src[i], j - i, "REM")) {
                while (src[i] && o <= 250) { renum_buf[o] = src[i]; o = o + 1; i = i + 1; }
            } else {
                d = word_is(&src[i], j - i, "GOTO") || word_is(&src[i], j - i, "GOSUB") ||
                    word_is(&src[i], j - i, "THEN");
                while (i < j && o <= 250) { renum_buf[o] = src[i]; o = o + 1; i = i + 1; }
                if (d) {
                    while (src[i] == 32 && o <= 250) { renum_buf[o] = 32; o = o + 1; i = i + 1; }
                    if (is_digit(src[i])) {
                        j = i;
                        num = 0;
                        big = 0;
                        while (is_digit(src[j])) {
                            /* past 65535 names no line, so it stays as typed */
                            if (num > 6553 || (num == 6553 && src[j] > 53)) big = 1;
                            else num = num * 10 + (src[j] - 48);
                            j = j + 1;
                        }
                        nn = (num && !big) ? renum_of(num, start, step) : 0;
                        if (nn) {
                            nd = 0;
                            while (nn) { digits[nd] = 48 + nn % 10; nd = nd + 1; nn = nn / 10; }
                            while (nd && o <= 250) { nd = nd - 1; renum_buf[o] = digits[nd]; o = o + 1; }
                            i = j;
                        } else {
                            while (i < j && o <= 250) { renum_buf[o] = src[i]; o = o + 1; i = i + 1; }
                        }
                    }
                }
            }
        } else {
            renum_buf[o] = c; o = o + 1; i = i + 1;
        }
    }
    if (o > 250) return 251;
    renum_buf[o] = 0;
    return o;
}

void ed_renum(unsigned int start, unsigned int step)
{
    unsigned int p;
    unsigned int n;
    unsigned int len;
    unsigned int total;

    /* The last number must fit: stop before any addition passes 65535. */
    p = 0;
    n = start;
    while (lineno_at(p)) {
        p = p + prog[p + 2];
        if (lineno_at(p)) {
            if (n > 65535 - step) { rt_error(E_RENUM); return; }
            n = n + step;
        }
    }
    /* Measure first, so a line or the program that would grow too long
     * stops RENUM before anything has changed.
     */
    p = 0;
    total = prog_len;
    while (lineno_at(p)) {
        len = renum_text(p, start, step);
        if (len > 250) { rt_error(E_LINELONG); return; }
        total = total + len + 4 - prog[p + 2];
        if (total >= PROGMAX) { rt_error(E_MEMORY); return; }
        p = p + prog[p + 2];
    }
    /* The references, while the lines still have their old numbers. */
    p = 0;
    while (lineno_at(p)) {
        len = renum_text(p, start, step);
        if (len + 4 != prog[p + 2]) ed_store(lineno_at(p), renum_buf);
        else memcpy(&prog[p + 3], renum_buf, len);
        p = p + prog[p + 2];
    }
    /* Then the numbers themselves. */
    p = 0;
    n = start;
    while (lineno_at(p)) {
        prog[p] = n >> 8;
        prog[p + 1] = n;
        n = n + step;
        p = p + prog[p + 2];
    }
}
