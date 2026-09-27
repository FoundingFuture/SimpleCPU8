
#include "basic.h"

/* DATA lines. The program stays text, and one parser reads it for RUN,
 * DATA(n) and READ, so the three cannot disagree about what a line holds.
 * docs/basic-data-design.md is the design.
 */

#define LINE_AT(p) ((prog[(p)] << 8) | prog[(p) + 1])

#define V_NUM 1
#define V_STR 2

/* The value read last: its kind, a number as written, whether that number
 * fits in a byte, and a string's place and length in lx_text.
 */
static unsigned char dv_kind;
static int dv_num;
static unsigned char dv_byte;
static unsigned int dv_str;
static unsigned char dv_len;

/* The address a line opened with, when line_fixed says it has one. */
static unsigned int line_addr;
static unsigned char line_fixed;

/* The drop cursor. Until an address moves it, it is in the program's data
 * area, which ends at the top of program memory.
 */
static unsigned int dt_at;
static unsigned char dt_fixed;

/* The line the caller was on, for the message after an error that is not
 * the DATA line's own.
 */
static int sv_line;

/* Is the line at p a DATA line. Leaves the lexer on its first word. */
static unsigned char is_data(unsigned int p)
{
    lx_raw = 1;
    lx_start((char *)&prog[p + 3]);
    return lx_is("DATA");
}

/* Past a value: a comma before the next one, or the end of the line. */
static unsigned char sep(void)
{
    if (lx_tok == T_END) return 1;
    if (lx_is(",")) {
        lx_next();
        if (lx_tok == T_END) { rt_expect("A NUMBER OR A STRING"); return 0; }
        return 1;
    }
    rt_expect(", OR THE END OF THE LINE");
    return 0;
}

/* Past the DATA word of the line at p, which the lexer stands on, and past
 * its address when it opens with one. Returns 0 after an error.
 */
static unsigned char open_line(unsigned int p)
{
    unsigned int digits;
    err_line = LINE_AT(p);
    line_fixed = 0;
    lx_next();
    if (lx_tok != T_NUM || lx_text[lx_tokpos] != 36) return 1;
    /* The way the number is written decides: $E000 and $0010 are
     * addresses, $10 is a value.
     */
    digits = lx_pos - lx_tokpos - 1;
    if (digits != 3 && digits != 4) return 1;
    line_addr = lx_num;
    line_fixed = 1;
    lx_next();
    return sep();
}

/* One value at the lexer into dv_*, and step past it. Returns 0 after a
 * syntax error.
 */
static unsigned char value(void)
{
    unsigned int at;
    unsigned int end;
    unsigned char hex;
    unsigned char neg;
    neg = 0;
    if (lx_is("-")) { neg = 1; lx_next(); }
    if (lx_tok == T_STR && !neg) {
        dv_kind = V_STR;
        dv_str = lx_str;
        dv_len = lx_len;
        lx_next();
        return 1;
    }
    if (lx_tok != T_NUM) { rt_expect("A NUMBER OR A STRING"); return 0; }
    dv_kind = V_NUM;
    dv_num = neg ? -lx_num : lx_num;
    /* Count the digits that matter. The lexer wraps at 16 bits, so 65546
     * reads as 10, and only the text says it is not a byte.
     */
    at = lx_tokpos;
    end = lx_pos;
    hex = lx_text[at] == 36;
    if (hex) at = at + 1;
    while (at + 1 < end && lx_text[at] == 48) at = at + 1;
    if (end - at > (hex ? 2 : 3)) dv_byte = 0;
    else if (neg) dv_byte = lx_num <= 128;
    else dv_byte = lx_num <= 255;
    lx_next();
    return 1;
}

/* A byte at the drop cursor, written when place is 1. */
static unsigned char drop(unsigned char b, unsigned char place)
{
    if (!dt_fixed && dt_at >= (unsigned int)prog + PROGMAX) { rt_error(E_MEMORY); return 0; }
    if (place) poke(dt_at, b);
    dt_at = dt_at + 1;
    return 1;
}

/* Walk the DATA lines in order, placing each byte when place is 1. Stop at
 * the line at offset stop once its address is taken, or at the end.
 */
static void walk(unsigned int stop, unsigned char place)
{
    unsigned int p;
    unsigned char i;
    dt_at = (unsigned int)prog + prog_len;
    dt_fixed = 0;
    p = 0;
    while (LINE_AT(p)) {
        if (is_data(p)) {
            if (!open_line(p)) return;
            if (line_fixed) { dt_at = line_addr; dt_fixed = 1; }
            if (p == stop) return;
            while (lx_tok != T_END) {
                if (!value()) return;
                if (dv_kind == V_STR) {
                    for (i = 0; i < dv_len; i++) {
                        if (!drop(lx_text[dv_str + i], place)) return;
                    }
                } else {
                    if (!dv_byte) { rt_error(E_DATABYTE); return; }
                    if (!drop(dv_num, place)) return;
                }
                if (!sep()) return;
            }
        }
        p = p + prog[p + 2];
    }
}

void dt_pack(void)
{
    sv_line = 0;
    walk(PROGMAX, 1);
    lx_raw = 0;
}
