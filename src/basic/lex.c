
#include "basic.h"

char *lx_text;
unsigned int lx_pos;
unsigned char lx_tok;
int lx_num;
char lx_word[12];
unsigned int lx_str;
unsigned char lx_len;

static unsigned char upper(unsigned char c)
{
    if (c >= 97 && c <= 122) return c - 32;
    return c;
}

static unsigned char isdig(unsigned char c) { return c >= 48 && c <= 57; }

static unsigned char isalpha(unsigned char c)
{
    c = upper(c);
    return c >= 65 && c <= 90;
}

static unsigned char ishex(unsigned char c)
{
    c = upper(c);
    return isdig(c) || (c >= 65 && c <= 70);
}

static unsigned char hexval(unsigned char c)
{
    c = upper(c);
    if (c >= 65) return c - 55;
    return c - 48;
}

void lx_start(char *text)
{
    lx_text = text;
    lx_pos = 0;
    lx_next();
}

void lx_next(void)
{
    unsigned char c;
    unsigned char i;
    unsigned int start;

    while (lx_text[lx_pos] == 32) lx_pos = lx_pos + 1;
    c = lx_text[lx_pos];

    if (c == 0) { lx_tok = T_END; lx_len = 0; return; }

    if (isdig(c)) {
        lx_num = 0;
        while (isdig(lx_text[lx_pos])) {
            lx_num = lx_num * 10 + (lx_text[lx_pos] - 48);
            lx_pos = lx_pos + 1;
        }
        lx_tok = T_NUM;
        lx_len = 0;
        return;
    }

    /* A hex number, $F000, the way an address is written in a listing. The
     * dollar after a name belongs to the name and is taken below, so a
     * dollar seen here starts a number. Sixteen bits wrap, as decimal does.
     */
    if (c == 36 && ishex(lx_text[lx_pos + 1])) {
        lx_num = 0;
        lx_pos = lx_pos + 1;
        while (ishex(lx_text[lx_pos])) {
            lx_num = (lx_num << 4) | hexval(lx_text[lx_pos]);
            lx_pos = lx_pos + 1;
        }
        lx_tok = T_NUM;
        lx_len = 0;
        return;
    }

    if (c == 34) {
        /* A string literal goes on the heap now, because that is where every
         * string lives and the collector has to be able to find it.
         */
        lx_pos = lx_pos + 1;
        start = lx_pos;
        i = 0;
        while (lx_text[lx_pos] != 34 && lx_text[lx_pos] != 0) {
            lx_pos = lx_pos + 1;
            i = i + 1;
        }
        lx_str = str_new(i);
        if (lx_str) {
            unsigned char j;
            for (j = 0; j < i; j++) heap[lx_str + 1 + j] = lx_text[start + j];
        }
        if (lx_text[lx_pos] == 34) lx_pos = lx_pos + 1;
        lx_tok = T_STR;
        lx_len = 0;
        return;
    }

    if (isalpha(c)) {
        i = 0;
        while (isalpha(lx_text[lx_pos]) || isdig(lx_text[lx_pos])) {
            if (i < 10) { lx_word[i] = upper(lx_text[lx_pos]); i = i + 1; }
            lx_pos = lx_pos + 1;
        }
        /* A trailing dollar belongs to the name: A$ is not A. */
        if (lx_text[lx_pos] == 36 && i < 10) {
            lx_word[i] = 36;
            i = i + 1;
            lx_pos = lx_pos + 1;
        }
        lx_word[i] = 0;
        lx_len = i;
        lx_tok = T_NAME;
        return;
    }

    /* Two character operators first, so <= is one token and not two. */
    lx_word[0] = c;
    lx_word[1] = 0;
    lx_word[2] = 0;
    if (c == 60 || c == 62) {
        if (lx_text[lx_pos + 1] == 61 || (c == 60 && lx_text[lx_pos + 1] == 62)) {
            lx_word[1] = lx_text[lx_pos + 1];
            lx_pos = lx_pos + 1;
        }
    }
    lx_pos = lx_pos + 1;
    lx_tok = T_PUNCT;
    lx_len = lx_word[1] ? 2 : 1;
}

unsigned char lx_is(char *word)
{
    unsigned char i;
    if (lx_tok != T_NAME && lx_tok != T_PUNCT) return 0;
    i = 0;
    while (word[i]) {
        if (lx_word[i] != word[i]) return 0;
        i = i + 1;
    }
    return lx_word[i] == 0;
}
