
#include "basic.h"

/* Precedence, loosest first: OR, AND, the comparisons, + -, * / MOD, unary.
 * One function per level, which is how the C compiler above does it too, so
 * the two read the same way in the layers pane.
 */

static int ex_or(void);

/* A variable's slot from its name. A is 0, A0 is 1, A9 is 10, B is 11. */
int var_slot(void)
{
    int base;
    base = (lx_word[0] - 65) * 11;
    if (lx_word[1] >= 48 && lx_word[1] <= 57) return base + 1 + (lx_word[1] - 48);
    return base;
}

static int fn_call(void)
{
    int a;
    unsigned int s;

    if (lx_is("RND")) {
        lx_next();
        a = 0;
        if (lx_is("(")) { lx_next(); a = ex_int(); if (lx_is(")")) lx_next(); }
        if (a <= 0) return rand();
        return rand() % a;
    }
    if (lx_is("ABS")) {
        lx_next();
        if (lx_is("(")) lx_next();
        a = ex_int();
        if (lx_is(")")) lx_next();
        if (a < 0) return -a;
        return a;
    }
    if (lx_is("LEN")) {
        lx_next();
        if (lx_is("(")) lx_next();
        s = ex_str();
        if (lx_is(")")) lx_next();
        return str_len(s);
    }
    if (lx_is("ASC")) {
        lx_next();
        if (lx_is("(")) lx_next();
        s = ex_str();
        if (lx_is(")")) lx_next();
        return str_at(s, 0);
    }
    if (lx_is("VAL")) {
        lx_next();
        if (lx_is("(")) lx_next();
        s = ex_str();
        if (lx_is(")")) lx_next();
        {
            unsigned int i;
            unsigned char neg;
            i = 0;
            neg = 0;
            a = 0;
            if (str_at(s, 0) == 45) { neg = 1; i = 1; }
            while (i < str_len(s)) {
                unsigned char c;
                c = str_at(s, i);
                if (c < 48 || c > 57) break;
                a = a * 10 + (c - 48);
                i = i + 1;
            }
            if (neg) return -a;
            return a;
        }
    }
    if (lx_is("KEY")) {
        lx_next();
        if (lx_is("(")) { lx_next(); if (lx_is(")")) lx_next(); }
        {
            unsigned char k;
            k = key_get();
            if (k == 0) return 0;
            if (io_key_is_up(k)) return 0;
            return io_key_code(k);
        }
    }
    if (lx_is("PAD")) {
        lx_next();
        if (lx_is("(")) { lx_next(); if (lx_is(")")) lx_next(); }
        return io_pad();
    }
    if (lx_is("PEEK")) {
        lx_next();
        if (lx_is("(")) lx_next();
        a = ex_int();
        if (lx_is(")")) lx_next();
        return peek(a);
    }
    /* The word DOKE stores, so a driver's vector reads back as it was set. */
    if (lx_is("DEEK")) {
        lx_next();
        if (lx_is("(")) lx_next();
        a = ex_int();
        if (lx_is(")")) lx_next();
        return (peek(a) << 8) | peek(a + 1);
    }
    if (lx_is("POINT")) {
        lx_next();
        if (lx_is("(")) lx_next();
        a = ex_int();
        if (lx_is(",")) lx_next();
        {
            int y;
            y = ex_int();
            if (lx_is(")")) lx_next();
            return gpu_read_pixel(a >> 8, a, y >> 8, y);
        }
    }
    rt_error(E_SYNTAX);
    return 0;
}

static int primary(void)
{
    int v;

    if (lx_tok == T_NUM) { v = lx_num; lx_next(); return v; }

    if (lx_is("(")) {
        lx_next();
        v = ex_or();
        if (lx_is(")")) lx_next(); else rt_error(E_SYNTAX);
        return v;
    }

    if (lx_is("-")) { lx_next(); return -primary(); }
    if (lx_is("+")) { lx_next(); return primary(); }
    if (lx_is("NOT")) { lx_next(); return primary() ? 0 : 1; }

    if (lx_tok == T_NAME) {
        /* A name with a dollar on it is a string, and a string is not an
         * integer. Everything else is a function or a variable.
         */
        if (IS_STRNAME) { rt_error(E_TYPE); return 0; }
        if (IS_INTVAR) {
            v = vars[var_slot()];
            lx_next();
            return v;
        }
        return fn_call();
    }

    rt_error(E_SYNTAX);
    return 0;
}

static int ex_mul(void)
{
    int a;
    int b;
    a = primary();
    for (;;) {
        if (lx_is("*")) { lx_next(); a = a * primary(); continue; }
        if (lx_is("/")) {
            lx_next();
            b = primary();
            if (b == 0) { rt_error(E_DIVZERO); return 0; }
            a = a / b;
            continue;
        }
        if (lx_is("MOD")) {
            lx_next();
            b = primary();
            if (b == 0) { rt_error(E_DIVZERO); return 0; }
            a = a % b;
            continue;
        }
        return a;
    }
}

static int ex_add(void)
{
    int a;
    a = ex_mul();
    for (;;) {
        if (lx_is("+")) { lx_next(); a = a + ex_mul(); continue; }
        if (lx_is("-")) { lx_next(); a = a - ex_mul(); continue; }
        return a;
    }
}

/* A string comparison and an integer one are told apart by what is in front:
 * a name ending in a dollar, or a literal.
 */
static unsigned char at_string(void)
{
    /* Anything whose name ends in a dollar is a string, function or
     * variable alike. That is BASIC's own rule and it is why STR$ and
     * CHR$ are spelled the way they are.
     */
    if (lx_tok == T_STR) return 1;
    if (IS_STRNAME) return 1;
    return 0;
}

static int ex_cmp(void)
{
    int a;
    int b;
    unsigned char op1;
    unsigned char op2;

    if (at_string()) {
        unsigned int s;
        s = ex_str();
        op1 = 0;
        op2 = 0;
        if (lx_tok == T_PUNCT) { op1 = lx_word[0]; op2 = lx_word[1]; }
        if (op1 != 60 && op1 != 62 && op1 != 61) { rt_error(E_TYPE); return 0; }
        lx_next();
        a = str_cmp(s, ex_str());
        if (op1 == 61) return a == 0;
        if (op1 == 60 && op2 == 62) return a != 0;
        if (op1 == 60 && op2 == 61) return a <= 0;
        if (op1 == 62 && op2 == 61) return a >= 0;
        if (op1 == 60) return a < 0;
        return a > 0;
    }

    a = ex_add();
    if (lx_tok != T_PUNCT) return a;
    op1 = lx_word[0];
    op2 = lx_word[1];
    if (op1 != 60 && op1 != 62 && op1 != 61) return a;
    lx_next();
    b = ex_add();
    if (op1 == 61) return a == b;
    if (op1 == 60 && op2 == 62) return a != b;
    if (op1 == 60 && op2 == 61) return a <= b;
    if (op1 == 62 && op2 == 61) return a >= b;
    if (op1 == 60) return a < b;
    return a > b;
}

static int ex_and(void)
{
    int a;
    a = ex_cmp();
    while (lx_is("AND")) { lx_next(); a = (a != 0) & (ex_cmp() != 0); }
    return a;
}

static int ex_or(void)
{
    int a;
    a = ex_and();
    while (lx_is("OR")) { lx_next(); a = (a != 0) | (ex_and() != 0); }
    return a;
}

int ex_int(void)
{
    return ex_or();
}

/* A string expression. Only concatenation, which is all a program needs and
 * all the heap can do without a second collector pass per operator.
 */
static unsigned int str_primary(void)
{
    unsigned int s;

    if (lx_tok == T_STR) { s = lx_str; lx_next(); return s; }

    if (lx_is("CHR$")) {
        lx_next();
        if (lx_is("(")) lx_next();
        {
            int v;
            v = ex_int();
            if (lx_is(")")) lx_next();
            s = str_new(1);
            if (s) heap[s + 1] = v;
            return s;
        }
    }
    if (lx_is("STR$")) {
        lx_next();
        if (lx_is("(")) lx_next();
        {
            int v;
            unsigned char buf[8];
            unsigned char n;
            unsigned int u;
            unsigned char neg;
            v = ex_int();
            if (lx_is(")")) lx_next();
            neg = 0;
            if (v < 0) { neg = 1; u = -v; } else u = v;
            n = 0;
            if (u == 0) { buf[0] = 48; n = 1; }
            while (u) { buf[n] = 48 + (u % 10); u = u / 10; n = n + 1; }
            s = str_new(n + neg);
            if (s == 0) return 0;
            if (neg) heap[s + 1] = 45;
            {
                unsigned char i;
                for (i = 0; i < n; i++) heap[s + 1 + neg + i] = buf[n - 1 - i];
            }
            return s;
        }
    }
    if (lx_is("MID$")) {
        lx_next();
        if (lx_is("(")) lx_next();
        {
            unsigned int src;
            int from;
            int count;
            unsigned int i;
            src = ex_str();
            if (lx_is(",")) lx_next();
            from = ex_int();
            count = 255;
            if (lx_is(",")) { lx_next(); count = ex_int(); }
            if (lx_is(")")) lx_next();
            if (from < 1) from = 1;
            if (from > str_len(src)) return 0;
            if (count > str_len(src) - from + 1) count = str_len(src) - from + 1;
            if (count < 0) return 0;
            s = str_new(count);
            if (s == 0) return 0;
            for (i = 0; i < count; i++) heap[s + 1 + i] = str_at(src, from - 1 + i);
            return s;
        }
    }
    if (lx_is("INKEY$")) {
        lx_next();
        {
            unsigned char k;
            k = key_get();
            if (k == 0 || io_key_is_up(k)) return 0;
            s = str_new(1);
            if (s) heap[s + 1] = io_key_code(k);
            return s;
        }
    }

    if (IS_STRVAR) {
        s = svar[lx_word[0] - 65];
        lx_next();
        return s;
    }

    rt_error(E_TYPE);
    return 0;
}

unsigned int ex_str(void)
{
    unsigned int a;
    a = str_primary();
    while (lx_is("+")) { lx_next(); a = str_cat(a, str_primary()); }
    return a;
}
