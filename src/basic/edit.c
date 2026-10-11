
#include "basic.h"
#include "keywords.h"

/* The store is one buffer. A table at its head names up to FILEMAX
 * files, and each loaded file has a block of records after it. A record
 * is the file's index, two bytes of line number, one of record length,
 * then the text. A record numbered zero ends its block, and BASIC has no
 * line zero, so nothing is lost by using it as the terminator. So the
 * line after a file's last is never another file's: RUN stops there.
 *
 * DESIGN: a new file's block goes at the end of the store. Loading one
 * moves no record of another, so every offset a program holds stays good.
 *
 * The text holds each keyword as one byte, KW_ in keywords.h, so the lexer
 * reads a keyword in one load. A number holds its value behind a marker,
 * KW_LITERAL, ahead of its digits, so the lexer never converts them. A
 * jump target is KW_TARGET, its file's index and its line. LIST, SAVE and
 * RENUM read the text back with ed_expand. Strings, the rest of a line
 * after REM, INCLUDE, FILE or a bang, and a DATA line stay as typed.
 * docs/design/basic-speed.md, proposals 2 and 4.
 */
unsigned char prog[PROGMAX];

/* One line as text, or as the stored bytes on their way in. */
char ed_line[256];

static unsigned int crunch(unsigned char *t, unsigned char *o);

static unsigned int lineno_at(unsigned int p)
{
    return (prog[p + R_NUM] << 8) | prog[p + R_NUM + 1];
}

unsigned int ed_file_start(unsigned char f)
{
    return (prog[FT_AT(f) + FNAME] << 8) | prog[FT_AT(f) + FNAME + 1];
}

static void set_start(unsigned char f, unsigned int at)
{
    prog[FT_AT(f) + FNAME] = at >> 8;
    prog[FT_AT(f) + FNAME + 1] = at;
}

char *ed_file_name(unsigned char f)
{
    return (char *)&prog[FT_AT(f)];
}

static unsigned char upper_of(unsigned char c)
{
    if (c >= 97 && c <= 122) return c - 32;
    return c;
}

void ed_file_rename(unsigned char f, char *name, unsigned char n)
{
    unsigned char i;
    char *o;
    o = ed_file_name(f);
    if (n > FNAME - 1) n = FNAME - 1;
    for (i = 0; i < n; i++) o[i] = upper_of(name[i]);
    for (i = n; i < FNAME; i++) o[i] = 0;
}

unsigned char ed_file_find(char *name, unsigned char n, unsigned char add)
{
    unsigned char f;
    unsigned char i;
    char *have;
    for (f = 0; f < prog[0]; f++) {
        have = ed_file_name(f);
        i = 0;
        while (i < n && have[i] == upper_of(name[i])) i = i + 1;
        if (i == n && have[n] == 0) return f;
    }
    if (!add || n == 0 || n > FNAME - 1) return 255;
    if (prog[0] >= FILEMAX) { rt_error(E_FILES); return 255; }
    f = prog[0];
    prog[0] = f + 1;
    ed_file_rename(f, name, n);
    set_start(f, 0);
    return f;
}

unsigned char ed_file_open(unsigned char f)
{
    unsigned int at;
    if (ed_file_start(f)) return 1;
    at = prog_len;
    if (at + R_END >= PROGMAX) { rt_error(E_MEMORY); return 0; }
    prog[at] = f;
    prog[at + R_NUM] = 0;
    prog[at + R_NUM + 1] = 0;
    prog[at + R_LEN] = R_END;
    prog_len = at + R_END;
    set_start(f, at);
    return 1;
}

/* The files whose blocks lie after at move by delta bytes. */
static void shift_files(unsigned int at, int delta)
{
    unsigned char f;
    unsigned int s;
    for (f = 0; f < prog[0]; f++) {
        s = ed_file_start(f);
        if (s > at) set_start(f, s + delta);
    }
}

/* Take len bytes out of the store at p. */
static void cut_at(unsigned int p, unsigned int len)
{
    memmove(&prog[p], &prog[p + len], prog_len - p - len);
    prog_len = prog_len - len;
    shift_files(p, 0 - len);
}

/* Where file f's block ends: on its end record. */
static unsigned int file_end(unsigned char f)
{
    unsigned int p;
    p = ed_file_start(f);
    while (lineno_at(p)) p = p + prog[p + R_LEN];
    return p;
}

void ed_file_close(unsigned char f)
{
    unsigned int s;
    s = ed_file_start(f);
    if (s == 0) return;
    set_start(f, 0);
    cut_at(s, file_end(f) + R_END - s);
    dt_restore();
}

void ed_reset(void)
{
    memset(prog, 0, FT_SIZE);
    prog[0] = 1;
    ed_file_rename(0, "MAIN", 4);
    prog_len = FT_SIZE;
    ed_file_open(0);
    edit_file = 0;
    run_file = 0;
    dt_restore();
}

void ed_new(void)
{
    unsigned int s;
    s = ed_file_start(edit_file);
    cut_at(s, file_end(edit_file) - s);
    dt_restore();
}

/* Where a line is, or where it would go. Lines are kept in order, so RUN and
 * LIST are a walk and GOTO is a search that can stop early.
 */
unsigned int ed_find_in(unsigned char f, int line)
{
    unsigned int p;
    p = ed_file_start(f);
    if (p == 0) return 0;
    while (lineno_at(p)) {
        if (lineno_at(p) >= line) return p;
        p = p + prog[p + R_LEN];
    }
    return p;
}

unsigned int ed_find(int line)
{
    return ed_find_in(edit_file, line);
}

/* Past the end records at p to the next line, or 0 at the store's end. */
static unsigned int skip_ends(unsigned int p)
{
    while (p < prog_len && lineno_at(p) == 0) p = p + R_END;
    if (p >= prog_len) return 0;
    return p;
}

unsigned int ed_first(void)
{
    return skip_ends(FT_SIZE);
}

unsigned int ed_next(unsigned int p)
{
    return skip_ends(p + prog[p + R_LEN]);
}

static void remove_at(unsigned int p)
{
    cut_at(p, prog[p + R_LEN]);
}

static void keywords_up(unsigned char *t);

void ed_store(int line, char *text)
{
    unsigned int p;
    unsigned int n;
    unsigned int rec;
    unsigned int i;

    /* READ's place is an offset into the program, so a line inserted,
     * replaced or deleted sends READ back to the first value.
     */
    p = ed_find(line);
    if (lineno_at(p) == line) { remove_at(p); dt_restore(); }

    n = 0;
    while (text[n]) n = n + 1;
    /* An empty line deletes: typing the number alone rubs the line out, the
     * way every BASIC has worked.
     */
    if (n == 0) return;
    if (n > 250) { rt_error(E_LINELONG); return; }
    n = crunch((unsigned char *)text, (unsigned char *)ed_line);
    if (err) return;
    rec = n + R_TEXT + 1;
    if (prog_len + rec >= PROGMAX) { rt_error(E_MEMORY); return; }

    memmove(&prog[p + rec], &prog[p], prog_len - p);
    shift_files(p, rec);
    prog[p] = edit_file;
    prog[p + R_NUM] = line >> 8;
    prog[p + R_NUM + 1] = line;
    prog[p + R_LEN] = rec;
    for (i = 0; i <= n; i++) prog[p + R_TEXT + i] = ed_line[i];
    prog_len = prog_len + rec;
    dt_restore();
}

/* LIST shows the current file's lines numbered from first to last, both
 * included. The walk starts at the first line not below first and stops
 * at the first line past last, since lines are in order.
 */
void ed_list(unsigned int first, unsigned int last)
{
    unsigned int p;
    unsigned int i;
    p = ed_find(first);
    while (lineno_at(p) && lineno_at(p) <= last) {
        term_putn(lineno_at(p));
        term_putc(32);
        ed_expand(&prog[p + R_TEXT], ed_line, edit_file);
        i = 0;
        while (ed_line[i]) { term_putc(ed_line[i]); i = i + 1; }
        term_nl();
        p = p + prog[p + R_LEN];
    }
}

/* ---- RENUM ---- */

static char renum_buf[256];

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
            if (j - i <= 8 && kw_find((char *)&t[i], j - i)) {
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

/* True when the text at t is a DATA line: its first word, read the way the
 * lexer reads a name, is DATA. data.c finds DATA lines by the same test.
 */
static unsigned char data_line(unsigned char *t)
{
    unsigned int n;
    while (*t == 32) t = t + 1;
    n = 0;
    while (is_letter(t[n]) || is_digit(t[n])) n = n + 1;
    if (t[n] == 36) n = n + 1;
    return word_is(t, n, "DATA");
}

static unsigned char hex_of(unsigned char c)
{
    c = upper_of(c);
    if (c >= 65) return c - 55;
    return c - 48;
}

/* The value of the number that starts at t[i], and where it ends. */
static unsigned int lit_val;

/* Where the number that starts at t[i] ends, read as the lexer reads one:
 * $ and hex digits, 0b and binary digits, or decimal digits. Its value
 * goes to lit_val, wrapped at 16 bits as the lexer wraps it.
 */
static unsigned int lit_end(unsigned char *t, unsigned int i)
{
    unsigned int v;
    v = 0;
    if (t[i] == 36) {
        i = i + 1;
        while (is_hex(t[i])) { v = (v << 4) | hex_of(t[i]); i = i + 1; }
    } else if (t[i] == 48 && (t[i + 1] == 98 || t[i + 1] == 66) && (t[i + 2] == 48 || t[i + 2] == 49)) {
        i = i + 2;
        while (t[i] == 48 || t[i] == 49) { v = (v << 1) | (t[i] - 48); i = i + 1; }
    } else {
        while (is_digit(t[i])) { v = v * 10 + (t[i] - 48); i = i + 1; }
    }
    lit_val = v;
    return i;
}

/* How many numbers and jump targets the last crunch_pass met. */
static unsigned char lit_count;

/* crunch_pass's place in the typed text and in the stored line, which
 * crunch_target moves on.
 */
static unsigned int cr_i;
static unsigned int cr_n;

/* After GOTO, GOSUB, THEN or RESTORE, with cr_i on the text after the
 * word: a line number, or a file's name, a colon and a line number. It
 * becomes KW_TARGET, the file's index and the line. A bare number names
 * the file the line is stored into. A number is a target only when the
 * end of the line or a colon follows it, so GOTO 10 * N stays an
 * expression. After THEN a number is always a line.
 *
 * A name the table lacks gets an entry here, not loaded, so the index
 * stands when the file comes in at RUN. A target past `marks` stays text,
 * which the statements still read.
 */
static void crunch_target(unsigned char *t, unsigned char *o, unsigned char marks, unsigned char then)
{
    unsigned int q;
    unsigned int s;
    unsigned int e;
    unsigned int v;
    unsigned char f;
    unsigned char named;
    q = cr_i;
    while (t[q] == 32) q = q + 1;
    s = q;
    named = 0;
    if (is_letter(t[q])) {
        while (is_letter(t[q]) || is_digit(t[q])) q = q + 1;
        if (t[q] != 58 || !is_digit(t[q + 1]) || q - s > FNAME - 1) return;
        named = 1;
        q = q + 1;
    }
    if (!is_digit(t[q])) return;
    e = q;
    v = 0;
    while (is_digit(t[e])) { v = v * 10 + (t[e] - 48); e = e + 1; }
    if (!named && !then) {
        q = e;
        while (t[q] == 32) q = q + 1;
        if (t[q] != 0 && t[q] != 58) return;
    }
    lit_count = lit_count + 1;
    f = edit_file;
    if (lit_count <= marks && named) {
        q = s;
        while (t[q] != 58) q = q + 1;
        f = ed_file_find((char *)&t[s], q - s, 1);
    }
    if (lit_count > marks || f == 255) {
        /* As typed, name and all: the name is not a keyword here. */
        while (cr_i < e) { o[cr_n] = t[cr_i]; cr_n = cr_n + 1; cr_i = cr_i + 1; }
        return;
    }
    while (cr_i < s) { o[cr_n] = 32; cr_n = cr_n + 1; cr_i = cr_i + 1; }
    o[cr_n] = KW_TARGET;
    o[cr_n + 1] = f;
    o[cr_n + 2] = v >> 8;
    o[cr_n + 3] = v;
    cr_n = cr_n + 4;
    cr_i = e;
}

/* The typed text t as a stored line at o, with its length returned. The
 * walk is keywords_up's, and a keyword it would put in capitals becomes
 * its byte. The first marks numbers and jump targets get their value
 * behind KW_LITERAL or KW_TARGET. A DATA line stays text, which data.c
 * reads as it always has.
 */
static unsigned int crunch_pass(unsigned char *t, unsigned char *o, unsigned char marks)
{
    unsigned int i;
    unsigned int j;
    unsigned int n;
    unsigned char k;
    i = 0;
    n = 0;
    lit_count = 0;
    if (data_line(t)) {
        while (t[i]) { o[n] = t[i]; n = n + 1; i = i + 1; }
        o[n] = 0;
        keywords_up(o);
        return n;
    }
    while (t[i]) {
        j = i + 1;
        if (t[i] == 34) {
            while (t[j] && t[j] != 34) j = j + 1;
            if (t[j]) j = j + 1;
        } else if ((t[i] == 36 && is_hex(t[i + 1])) || is_digit(t[i])) {
            /* A word that starts with a digit runs over letters, as the
             * IDE's does: 1TO stays. The number at its start is the one
             * the lexer reads, and the letters after it stay text.
             */
            if (t[i] == 36) {
                while (is_hex(t[j])) j = j + 1;
            } else {
                while (is_name(t[j]) || t[j] == 46) j = j + 1;
            }
            lit_end(t, i);
            if (lit_count < marks) {
                o[n] = KW_LITERAL;
                o[n + 1] = lit_val >> 8;
                o[n + 2] = lit_val;
                n = n + 3;
            }
            lit_count = lit_count + 1;
        } else if (is_letter(t[i]) || t[i] == 95) {
            while (is_name(t[j])) j = j + 1;
            if (t[j] == 36) j = j + 1;
            k = 0;
            if (j - i <= 8) k = kw_find((char *)&t[i], j - i);
            if (k) {
                o[n] = k;
                n = n + 1;
                i = j;
                /* The rest of a REM line is not BASIC, and a file's name
                 * after INCLUDE or FILE is no keyword.
                 */
                if (k == KW_REM || k == KW_INCLUDE || k == KW_FILE) j = i + 250;
                if (k == KW_GOTO || k == KW_GOSUB || k == KW_THEN || k == KW_RESTORE) {
                    cr_i = i;
                    cr_n = n;
                    crunch_target(t, o, marks, k == KW_THEN);
                    if (err) return n;
                    i = cr_i;
                    n = cr_n;
                    j = i;
                }
            }
        } else if (t[i] == 33) {
            j = i + 250;
        }
        while (i < j && t[i]) { o[n] = t[i]; n = n + 1; i = i + 1; }
    }
    o[n] = 0;
    return n;
}

/* The typed text t as a stored line at o, with its length returned.
 * crunchLine in program.cpp is the IDE's copy of this rule, and a test
 * holds the two to one result.
 *
 * DESIGN: a number's value costs 3 bytes and a target at most 3 more than
 * its digits, and a stored line holds 250 at most. The first numbers and
 * targets of the line get their marker while the line fits. One past
 * that stays text, which the lexer and the statements still read.
 */
static unsigned int crunch(unsigned char *t, unsigned char *o)
{
    unsigned int n;
    unsigned char marks;
    n = crunch_pass(t, o, 0);
    marks = lit_count;
    if (n + 3 * marks > 250) marks = (250 - n) / 3;
    if (marks == 0) return n;
    return crunch_pass(t, o, marks);
}

/* ed_expand's place in its answer. A line that would pass 250 characters
 * is cut there, since ed_line holds 256.
 */
static char *ex_o;
static unsigned int ex_n;

static void ex_put(unsigned char c)
{
    if (ex_n >= 250) return;
    ex_o[ex_n] = c;
    ex_n = ex_n + 1;
}

static void ex_number(unsigned int v)
{
    unsigned char digits[6];
    unsigned char d;
    d = 0;
    do { digits[d] = 48 + (v % 10); v = v / 10; d = d + 1; } while (v);
    while (d) { d = d - 1; ex_put(digits[d]); }
}

unsigned int ed_expand(unsigned char *t, char *o, unsigned char own)
{
    unsigned int i;
    unsigned char c;
    char *w;
    i = 0;
    ex_o = o;
    ex_n = 0;
    if (data_line(t)) {
        while (t[i]) { ex_put(t[i]); i = i + 1; }
        o[ex_n] = 0;
        return ex_n;
    }
    while (t[i]) {
        c = t[i];
        i = i + 1;
        /* A number's value, which may hold any byte, 0 included. Its
         * digits follow as text.
         */
        if (c == KW_LITERAL) {
            i = i + 2;
            continue;
        }
        /* A jump target: the file's name and a colon when the file is
         * another one, then the line.
         */
        if (c == KW_TARGET) {
            if (t[i] != own) {
                w = ed_file_name(t[i]);
                while (*w) { ex_put(*w); w = w + 1; }
                ex_put(58);
            }
            ex_number((t[i + 1] << 8) | t[i + 2]);
            i = i + 3;
            continue;
        }
        if (c >= KW_FIRST) {
            w = kw_text(c);
            while (*w != 32) { ex_put(*w); w = w + 1; }
            if (c == KW_REM || c == KW_INCLUDE || c == KW_FILE) c = 33;
        } else {
            ex_put(c);
        }
        if (c == 34) {
            while (t[i] && t[i] != 34) { ex_put(t[i]); i = i + 1; }
            if (t[i]) { ex_put(34); i = i + 1; }
        } else if (c == 33) {
            while (t[i]) { ex_put(t[i]); i = i + 1; }
        }
    }
    o[ex_n] = 0;
    return ex_n;
}

/* The number line `old` gets, or 0 when there is no such line. */
static unsigned int renum_of(unsigned int old, unsigned int start, unsigned int step)
{
    unsigned int p;
    unsigned int n;
    p = ed_file_start(edit_file);
    n = start;
    while (lineno_at(p)) {
        if (lineno_at(p) == old) return n;
        n = n + step;
        p = p + prog[p + R_LEN];
    }
    return 0;
}

/* The targets in the record at p that name a line of the current file,
 * given their new numbers. A target keeps its size, so nothing moves.
 * The lines of the other files are changed this way.
 */
static void renum_targets(unsigned int p, unsigned int start, unsigned int step)
{
    unsigned char *t;
    unsigned int i;
    unsigned int nn;
    unsigned char c;
    t = &prog[p + R_TEXT];
    if (data_line(t)) return;
    i = 0;
    while (t[i]) {
        c = t[i];
        if (c == KW_LITERAL) { i = i + 3; continue; }
        if (c == KW_TARGET) {
            if (t[i + 1] == edit_file) {
                nn = renum_of((t[i + 2] << 8) | t[i + 3], start, step);
                if (nn) { t[i + 2] = nn >> 8; t[i + 3] = nn; }
            }
            i = i + 4;
            continue;
        }
        if (c == KW_REM || c == KW_INCLUDE || c == KW_FILE || c == 33) return;
        i = i + 1;
        if (c == 34) {
            while (t[i] && t[i] != 34) i = i + 1;
            if (t[i]) i = i + 1;
        }
    }
}

/* The text of the line at p with its references renumbered, into
 * renum_buf. A reference is the number after GOTO, GOSUB, THEN, RESTORE or
 * DATA(. Returns its length, or 251 when it would be too long. Strings
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
    unsigned char dp;
    unsigned int k;
    /* The words as text, so the scan below reads what LIST shows. */
    ed_expand(&prog[p + R_TEXT], ed_line, edit_file);
    src = (unsigned char *)ed_line;
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
                    word_is(&src[i], j - i, "THEN") || word_is(&src[i], j - i, "RESTORE");
                /* DATA( names a line. DATA and a value is a DATA line. */
                dp = word_is(&src[i], j - i, "DATA");
                while (i < j && o <= 250) { renum_buf[o] = src[i]; o = o + 1; i = i + 1; }
                if (dp) {
                    k = i;
                    while (src[k] == 32) k = k + 1;
                    if (src[k] == 40) {
                        while (i <= k && o <= 250) { renum_buf[o] = src[i]; o = o + 1; i = i + 1; }
                        d = 1;
                    }
                }
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
    unsigned int first;
    unsigned int p;
    unsigned int n;
    unsigned int len;
    unsigned int total;

    first = ed_file_start(edit_file);
    dt_restore();
    /* The last number must fit: stop before any addition passes 65535. */
    p = first;
    n = start;
    while (lineno_at(p)) {
        p = p + prog[p + R_LEN];
        if (lineno_at(p)) {
            if (n > 65535 - step) { rt_error(E_RENUM); return; }
            n = n + step;
        }
    }
    /* Measure first, so a line or the program that would grow too long
     * stops RENUM before anything has changed.
     */
    p = first;
    total = prog_len;
    while (lineno_at(p)) {
        len = renum_text(p, start, step);
        if (len > 250) { rt_error(E_LINELONG); return; }
        len = crunch((unsigned char *)renum_buf, (unsigned char *)ed_line);
        total = total + len + R_TEXT + 1 - prog[p + R_LEN];
        if (total >= PROGMAX) { rt_error(E_MEMORY); return; }
        p = p + prog[p + R_LEN];
    }
    /* The other files' targets into this one, while its lines still have
     * their old numbers.
     */
    p = ed_first();
    while (p) {
        if (prog[p] != edit_file) renum_targets(p, start, step);
        p = ed_next(p);
    }
    /* This file's own references. The text is stored again as a typed
     * line is, and a line stored again moves the ones after it.
     */
    p = ed_file_start(edit_file);
    while (lineno_at(p)) {
        renum_text(p, start, step);
        len = crunch((unsigned char *)renum_buf, (unsigned char *)ed_line);
        if (len + R_TEXT + 1 != prog[p + R_LEN]) ed_store(lineno_at(p), renum_buf);
        else memcpy(&prog[p + R_TEXT], ed_line, len);
        p = p + prog[p + R_LEN];
    }
    /* Then the numbers themselves. */
    p = ed_file_start(edit_file);
    n = start;
    while (lineno_at(p)) {
        prog[p + R_NUM] = n >> 8;
        prog[p + R_NUM + 1] = n;
        n = n + step;
        p = p + prog[p + R_LEN];
    }
}
