#include "basic.h"

/* The storage driver: LOAD, SAVE, DELETE and CATALOG over the cartridge's
 * named slots, through the storage device. A slot holds the text LIST
 * prints. The IDE's editor and this driver read the same thing. FILE and
 * INCLUDE bring a slot into the store as a file beside the main one.
 */

/* The text goes through one block, larger than PROGMAX. A listing spends
 * five digits and a space per line where a record spends three bytes. A
 * keyword takes one byte stored and its word listed, and a number three
 * more bytes stored than listed. The Centipede prototypes list at 0.89 to
 * 1.20 times their stored size. TEXTMAX keeps the 4 to 3 over PROGMAX the
 * block always had, rounded up to 22 KB.
 */
static char textbuf[TEXTMAX];

/* A program's name or its path, as LOAD and SAVE were given it. A path
 * is longer than a slot's name, and the device says which names it takes.
 */
#define NAMEMAX 96
static char name_buf[NAMEMAX + 1];

/* A file's name in the store: capitals, 16 characters at most. */
static char file_buf[FNAME];
static unsigned char file_len;

static unsigned char upper(unsigned char c)
{
    if (c >= 97 && c <= 122) return c - 32;
    return c;
}

/* The file a name or a path stands for, into file_buf: what follows the
 * last slash, up to its first dot, in capitals. So "../lib/graphics.bas"
 * and "graphics" are both the file GRAPHICS.
 */
static void file_of(char *name)
{
    unsigned char i;
    unsigned char from;
    from = 0;
    for (i = 0; name[i]; i++) {
        if (name[i] == 47 || name[i] == 92) from = i + 1;
    }
    file_len = 0;
    for (i = from; name[i] && name[i] != 46 && file_len < FNAME - 1; i++) {
        file_buf[file_len] = upper(name[i]);
        file_len = file_len + 1;
    }
    file_buf[file_len] = 0;
}

/* The quoted name after the command word. 1 when there is one, 0 when
 * the line ends there, which SAVE allows. Anything else is a mistake.
 */
static unsigned char take_name(unsigned char optional)
{
    unsigned char n;
    unsigned char i;
    name_buf[0] = 0;
    if (lx_tok == T_END && optional) return 0;
    if (lx_tok != T_STR) { rt_expect("A PROGRAM NAME IN QUOTES"); return 0; }
    n = str_len(lx_str);
    if (n > NAMEMAX) n = NAMEMAX;
    for (i = 0; i < n; i++) name_buf[i] = heap[lx_str + 1 + i];
    name_buf[n] = 0;
    return 1;
}

static unsigned char check(void)
{
    unsigned char s;
    s = sto_status();
    if (s == STO_OK) return 1;
    if (s == STO_NOT_FOUND) { rt_error(E_NOTFOUND); rt_found(name_buf); }
    else if (s == STO_FULL) rt_error(E_STOFULL);
    else if (s == STO_BAD_NAME) rt_error(E_BADNAME);
    else rt_error(E_SYNTAX);
    return 0;
}

/* The current file's listing, as ed_list prints it, into the block. */
static unsigned char serialise(void)
{
    unsigned int p;
    unsigned int at;
    unsigned int n;
    unsigned int i;
    at = 0;
    p = ed_file_start(edit_file);
    while (prog[p + R_NUM] || prog[p + R_NUM + 1]) {
        unsigned char digits[6];
        unsigned char d;
        n = (prog[p + R_NUM] << 8) | prog[p + R_NUM + 1];
        d = 0;
        while (n) { digits[d] = 48 + (n % 10); n = n / 10; d = d + 1; }
        /* The keywords as text, the way LIST shows them. The test is the
         * one a line of that text as its own record would take.
         */
        n = ed_expand(&prog[p + R_TEXT], ed_line, edit_file);
        if (at + d + n + 6 >= TEXTMAX) { rt_error(E_SAVEBIG); return 0; }
        while (d) { d = d - 1; textbuf[at] = digits[d]; at = at + 1; }
        textbuf[at] = 32;
        at = at + 1;
        i = 0;
        while (ed_line[i]) { textbuf[at] = ed_line[i]; at = at + 1; i = i + 1; }
        textbuf[at] = 10;
        at = at + 1;
        p = p + prog[p + R_LEN];
    }
    textbuf[at] = 0;
    return 1;
}

/* Every numbered line goes into the current file the way a typed one
 * does. A line with no number is skipped. A saved listing has none, and
 * running one while loading would be a surprise.
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
            if (err) { err_line = n; return; }
        }
    }
}

/* The text in the block into file f, which becomes the current file
 * while its lines go in. A file that does not fit is taken out again,
 * since half a file is worse than none, and the error stands.
 */
static void enter_file(unsigned char f)
{
    unsigned char was;
    if (!ed_file_open(f)) return;
    was = edit_file;
    edit_file = f;
    enter_lines();
    edit_file = was;
    if (err) {
        run_file = f;
        if (f) ed_file_close(f);
    }
}

/* The file whose name is in file_buf, brought in from the slot or the
 * path called name when the store lacks it. 255 when there is none
 * anywhere, with no error, and after an error.
 */
static unsigned char fetch(char *name)
{
    unsigned char f;
    f = ed_file_find(file_buf, file_len, 0);
    if (f != 255 && ed_file_start(f)) return f;
    sto_load(textbuf, name, TEXTMAX);
    if (sto_status() == STO_NOT_FOUND) return 255;
    if (!check()) return 255;
    f = ed_file_find(file_buf, file_len, 1);
    if (f == 255) return 255;
    enter_file(f);
    if (err) return 255;
    return f;
}

/* LOAD "NAME" brings a slot in as a file and makes it current. A file of
 * that name in the store is read again from its slot. With the main file
 * empty the program becomes the main file, the one RUN runs. Otherwise it
 * comes in beside the files that are there.
 */
static void do_load(void)
{
    unsigned char f;
    sto_load(textbuf, name_buf, TEXTMAX);
    if (!check()) return;
    file_of(name_buf);
    /* A running program's lines may move or go, so a program that loads
     * stops here.
     */
    running = 0;
    f = ed_file_find(file_buf, file_len, 0);
    if (f != 255) {
        /* A file of that name, or a name a line gave an entry. */
        if (ed_file_start(f)) {
            edit_file = f;
            ed_new();
        }
    } else if (prog[ed_file_start(0) + R_NUM] == 0 && prog[ed_file_start(0) + R_NUM + 1] == 0) {
        f = 0;
        ed_file_rename(0, file_buf, file_len);
    } else {
        f = ed_file_find(file_buf, file_len, 1);
        if (f == 255) return;
    }
    enter_file(f);
    if (!err) edit_file = f;
}

/* Wait for a key and echo it. True for Y. */
static unsigned char yes(void)
{
    unsigned char k;
    for (;;) {
        k = key_get();
        if (k == 0) continue;
        if (io_key_is_up(k)) continue;
        k = io_key_code(k);
        if (k >= 32) break;
    }
    term_putc(k);
    term_nl();
    return k == 89 || k == 121;
}

/* SAVE writes the current file into the slot of its own name, and SAVE
 * "NAME" into another. A slot that holds a program is asked about first.
 */
static void do_save(unsigned char named)
{
    unsigned char i;
    char *own;
    if (!named) {
        own = ed_file_name(edit_file);
        for (i = 0; own[i]; i++) name_buf[i] = own[i];
        name_buf[i] = 0;
    }
    /* The block is free until serialise fills it, so a LOAD into it says
     * whether the slot is taken. STO_FULL is a slot too large for it.
     */
    sto_load(textbuf, name_buf, TEXTMAX);
    if (sto_status() == STO_BAD_NAME) { check(); return; }
    if (sto_status() != STO_NOT_FOUND) {
        term_puts("OVERWRITE ");
        term_puts(name_buf);
        term_puts(" (Y/N)?");
        if (!yes()) return;
    }
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

/* A slot named AUTORUN runs at power on, so a ROM made from a BASIC
 * project boots straight into its program. Nothing is said when there is
 * no such slot: that is the plain interpreter starting up.
 */
void sto_autorun(void)
{
    sto_load(textbuf, "AUTORUN", TEXTMAX);
    if (sto_status() != STO_OK) return;
    ed_reset();
    ed_file_rename(0, "AUTORUN", 7);
    enter_lines();
    if (err) return;
    rt_run();
}

/* The file's name at text into file_buf: letters and digits after any
 * spaces, in capitals, with .BAS after it allowed and ignored. 0 with an
 * error when something else follows, or the name is too long.
 */
static unsigned char take_file(char *text)
{
    unsigned char i;
    unsigned char c;
    i = 0;
    while (text[i] == 32) i = i + 1;
    file_len = 0;
    for (;;) {
        c = upper(text[i]);
        if (!((c >= 48 && c <= 57) || (c >= 65 && c <= 90))) break;
        if (file_len >= FNAME - 1) { rt_error(E_BADNAME); return 0; }
        file_buf[file_len] = c;
        file_len = file_len + 1;
        i = i + 1;
    }
    file_buf[file_len] = 0;
    if (c == 46 && upper(text[i + 1]) == 66 && upper(text[i + 2]) == 65 && upper(text[i + 3]) == 83) i = i + 4;
    while (text[i] == 32) i = i + 1;
    if (text[i]) { lx_seek(text, i); rt_expect("THE END OF THE LINE"); return 0; }
    return 1;
}

/* INCLUDE's file, which must be somewhere: in the store, in a slot of
 * the cartridge's or among the libraries.
 */
static void include(char *text)
{
    if (!take_file(text)) return;
    if (file_len == 0) { lx_seek(text, 0); rt_expect("A FILE NAME AFTER INCLUDE"); return; }
    if (fetch(file_buf) == 255 && !err) { rt_error(E_NOFILE); rt_found(file_buf); }
}

void sto_include(char *text)
{
    include(text);
    /* A file that came in may INCLUDE others. */
    if (!err) sto_includes();
}

/* Every line that opens with INCLUDE, in every loaded file. A file in
 * the store is left alone, so a chain works and a cycle ends. A new
 * file's block goes after every other. So the walk stays where it is
 * while a file comes in, and reaches that file's own INCLUDE lines later
 * in the same pass. An error names the INCLUDE's line.
 */
void sto_includes(void)
{
    unsigned int p;
    unsigned char *t;
    p = ed_first();
    while (p) {
        t = &prog[p + R_TEXT];
        while (*t == 32) t = t + 1;
        if (*t == KW_INCLUDE) {
            include((char *)(t + 1));
            if (err) {
                /* A file that did not fit names its own line instead. */
                if (err == E_NOFILE || err == E_SYNTAX || err == E_BADNAME || err == E_FILES) {
                    err_line = (prog[p + R_NUM] << 8) | prog[p + R_NUM + 1];
                    run_file = prog[p];
                }
                return;
            }
        }
        p = ed_next(p);
    }
}

/* FILE NAME makes a file current: one in the store, else one a slot or a
 * library holds, else a new empty one. FILE alone prints the name.
 */
void sto_file(char *text)
{
    unsigned char f;
    if (!take_file(text)) return;
    if (file_len == 0) {
        term_puts(ed_file_name(edit_file));
        term_nl();
        return;
    }
    f = fetch(file_buf);
    if (err) return;
    if (f == 255) {
        f = ed_file_find(file_buf, file_len, 1);
        if (f == 255 || !ed_file_open(f)) return;
    }
    edit_file = f;
}

/* Returns 1 when the text was one of the four commands, whether or not it
 * went well. The chain stops at the first link that knows the word.
 */
unsigned char sto_bang(char *text)
{
    lx_start(text);
    if (lx_is("CATALOG")) { do_catalog(); return 1; }
    if (lx_is("LOAD")) { lx_next(); if (take_name(0)) do_load(); return 1; }
    if (lx_is("SAVE")) {
        unsigned char named;
        lx_next();
        named = take_name(1);
        if (!err) do_save(named);
        return 1;
    }
    if (lx_is("DELETE")) {
        lx_next();
        if (take_name(0)) { sto_delete(name_buf); check(); }
        return 1;
    }
    return 0;
}
