
#include "basic.h"
#include "keywords.h"

char *lx_text;
unsigned int lx_pos;
unsigned char lx_tok;
int lx_num;
char lx_word[12];
unsigned int lx_str;
unsigned char lx_len;
unsigned int lx_tokpos;
unsigned char lx_raw;
unsigned char lx_kw;

/* The reserved words, and where each starts in the list. lx_init fills
 * kw_at once at power on, so a word's text is one load away.
 */
static char *kw_all;
static unsigned int kw_at[KW_COUNT];

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

static unsigned char isbin(unsigned char c) { return c == 48 || c == 49; }

/* True when the text at t opens a binary number: 0b and a binary digit. */
static unsigned char at_binary(unsigned char *t)
{
    return t[0] == 48 && (t[1] == 98 || t[1] == 66) && isbin(t[2]);
}

/* Past the digits of the number that starts at t: $ and hex digits, 0b and
 * binary digits, or decimal digits. The lexer reads a number over the
 * same extent, so a stored literal's digits end where they ended when
 * typed.
 */
static unsigned char *past_number(unsigned char *t)
{
    if (*t == 36) {
        t = t + 1;
        while (ishex(*t)) t = t + 1;
    } else if (at_binary(t)) {
        t = t + 2;
        while (isbin(*t)) t = t + 1;
    } else {
        while (isdig(*t)) t = t + 1;
    }
    return t;
}

static unsigned char hexval(unsigned char c)
{
    c = upper(c);
    if (c >= 65) return c - 55;
    return c - 48;
}

void lx_init(void)
{
    unsigned int i;
    unsigned char n;
    kw_all = BASIC_KEYWORDS;
    i = 1;
    n = 0;
    while (kw_all[i]) {
        kw_at[n] = i;
        n = n + 1;
        while (kw_all[i] != 32) i = i + 1;
        i = i + 1;
    }
}

char *kw_text(unsigned char k)
{
    return &kw_all[kw_at[k - KW_FIRST]];
}

unsigned char kw_find(char *w, unsigned char n)
{
    unsigned char k;
    unsigned char i;
    char *t;
    for (k = 0; k < KW_COUNT; k++) {
        t = &kw_all[kw_at[k]];
        i = 0;
        while (i < n && t[i] == upper(w[i])) i = i + 1;
        if (i == n && t[n] == 32) return KW_FIRST + k;
    }
    return 0;
}

void lx_start(char *text)
{
    lx_seek(text, 0);
}

/* Start reading part way along a line. NEXT uses it to pick a FOR's body
 * up again without re-reading the FOR.
 */
void lx_seek(char *text, unsigned int pos)
{
    lx_text = text;
    lx_pos = pos;
    lx_next();
}

void lx_next(void)
{
    unsigned char c;
    unsigned char i;
    unsigned int start;

    /* lx_kw is 0 but for a keyword, and lx_word[0] 0 but for a name or
     * punctuation, so IS_PUNCT and a test of lx_kw need no lx_tok first.
     */
    lx_kw = 0;
    lx_word[0] = 0;
    while (lx_text[lx_pos] == 32) lx_pos = lx_pos + 1;
    lx_tokpos = lx_pos;
    c = lx_text[lx_pos];

    if (c == 0) { lx_tok = T_END; lx_len = 0; return; }

    /* A number a stored line holds with its value, docs/design/basic-speed.md
     * proposal 4. The word loads in one instruction, and the digits after
     * it are stepped over, never converted.
     */
    if (c == KW_LITERAL) {
        unsigned char *t;
        t = (unsigned char *)lx_text + lx_pos;
        lx_num = *(int *)(t + 1);
        t = t + 3;
        /* Most numbers are decimal and start with 1 to 9. A 0 may open
         * 0b, and a $ opens hex.
         */
        if (*t > 48 && *t <= 57) {
            t = t + 1;
            while (*t >= 48 && *t <= 57) t = t + 1;
        } else {
            t = past_number(t);
        }
        lx_pos = (unsigned int)t - (unsigned int)lx_text;
        lx_tok = T_NUM;
        lx_len = 0;
        return;
    }

    /* A keyword a stored line holds as its byte. */
    if (c >= KW_FIRST) {
        lx_kw = c;
        lx_tok = T_KEY;
        lx_len = 0;
        lx_pos = lx_pos + 1;
        return;
    }

    if (isdig(c)) {
        /* A binary number, 0b1010, the way a bit pattern reads. Sixteen
         * bits wrap, as decimal does.
         */
        if (c == 48 && at_binary((unsigned char *)&lx_text[lx_pos])) {
            lx_num = 0;
            lx_pos = lx_pos + 2;
            while (isbin(lx_text[lx_pos])) {
                lx_num = (lx_num << 1) | (lx_text[lx_pos] - 48);
                lx_pos = lx_pos + 1;
            }
            lx_tok = T_NUM;
            lx_len = 0;
            return;
        }
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
        if (lx_raw) {
            lx_str = start;
            lx_len = i;
        } else {
            lx_str = str_new(i);
            if (lx_str) {
                unsigned char j;
                for (j = 0; j < i; j++) heap[lx_str + 1 + j] = lx_text[start + j];
            }
            lx_len = 0;
        }
        if (lx_text[lx_pos] == 34) lx_pos = lx_pos + 1;
        lx_tok = T_STR;
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
        /* A keyword written out: typed at the prompt, in a DATA line or a
         * bang's text, or run into a number, as TO in 1TO. A variable's
         * second character is never a letter, so a variable skips this.
         */
        if (i >= 2 && lx_word[1] >= 65) {
            lx_kw = kw_find(lx_word, i);
            if (lx_kw) lx_tok = T_KEY;
        }
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
    char *t;
    if (lx_tok == T_KEY) {
        t = kw_text(lx_kw);
        i = 0;
        while (word[i]) {
            if (t[i] != word[i]) return 0;
            i = i + 1;
        }
        return t[i] == 32;
    }
    if (lx_tok != T_NAME && lx_tok != T_PUNCT) return 0;
    i = 0;
    while (word[i]) {
        if (lx_word[i] != word[i]) return 0;
        i = i + 1;
    }
    return lx_word[i] == 0;
}

void lx_name(void)
{
    char *t;
    unsigned char i;
    if (lx_tok != T_KEY) return;
    t = kw_text(lx_kw);
    i = 0;
    while (t[i] != 32) {
        lx_word[i] = t[i];
        i = i + 1;
    }
    lx_word[i] = 0;
    lx_len = i;
    lx_tok = T_NAME;
    lx_kw = 0;
}
