
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

/* Where READ is: rd_on is 0 until the first value is found, then rd_line
 * is the offset of the DATA line and rd_pos where its next value starts.
 * rd_hash is the program's hash at the end of the last command.
 */
static unsigned char rd_on;
static unsigned int rd_line;
static unsigned int rd_pos;
static unsigned int rd_hash;

/* Whether rd_on was set right after dt_check ran, for dt_mark to read. */
static unsigned char rd_was;

/* The line the caller was on, for the message after an error that is not
 * the DATA line's own.
 */
static int sv_line;

/* The caller's place in its own line. A walk moves the lexer into DATA
 * lines, and lx_restore reads the caller's token again from its start.
 * The callers stand on a name, a bracket, a colon or the end of the line,
 * all of which read back the same.
 */
static char *sv_text;
static unsigned int sv_pos;

static void lx_save(void)
{
    sv_text = lx_text;
    sv_pos = lx_tokpos;
    sv_line = err_line;
}

/* After an error the lexer stays where it failed and err_line names the
 * line the error is in.
 */
static void lx_restore(void)
{
    lx_raw = 0;
    if (err) return;
    err_line = sv_line;
    lx_seek(sv_text, sv_pos);
}

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
    rd_on = 0;
    sv_line = 0;
    walk(PROGMAX, 1);
    lx_raw = 0;
}

int dt_addr(int line)
{
    unsigned int p;
    p = ed_find(line);
    if (line == 0 || LINE_AT(p) != line) { err_arg = line; rt_error(E_NOLINE); return 0; }
    lx_save();
    if (is_data(p)) walk(p, 0);
    else { err_arg = line; rt_error(E_NOTDATA); }
    lx_restore();
    return dt_at;
}

/* Stand the lexer on the next value READ takes. Returns 0 after an error. */
static unsigned char rd_seek(void)
{
    unsigned int p;
    p = 0;
    if (rd_on) {
        err_line = LINE_AT(rd_line);
        lx_raw = 1;
        lx_seek((char *)&prog[rd_line + 3], rd_pos);
        if (lx_tok != T_END) return 1;
        p = rd_line + prog[rd_line + 2];
    }
    while (LINE_AT(p)) {
        if (is_data(p)) {
            if (!open_line(p)) return 0;
            rd_on = 1;
            rd_line = p;
            rd_pos = lx_tokpos;
            if (lx_tok != T_END) return 1;
        }
        p = p + prog[p + 2];
    }
    err_line = sv_line;
    rt_error(E_NODATA);
    return 0;
}

/* The next value into dv_*, with the cursor moved past it. A READ of the
 * wrong kind still takes the value: rd_pos moves before dt_read_int and
 * dt_read_str check dv_kind, so the next READ goes on to the value after it.
 */
static unsigned char rd_next(void)
{
    if (!rd_seek()) return 0;
    if (!value()) return 0;
    if (!sep()) return 0;
    rd_pos = lx_tokpos;
    return 1;
}

int dt_read_int(void)
{
    int v;
    v = 0;
    lx_save();
    if (rd_next()) {
        if (dv_kind == V_STR) { err_line = sv_line; rt_error(E_TYPE); }
        else v = dv_num;
    }
    lx_restore();
    return v;
}

unsigned int dt_read_str(void)
{
    unsigned int s;
    unsigned char i;
    s = 0;
    lx_save();
    if (rd_next()) {
        if (dv_kind != V_STR) { err_line = sv_line; rt_error(E_NEEDSTR); }
        else if (dv_len) {
            s = str_new(dv_len);
            if (s) {
                for (i = 0; i < dv_len; i++) heap[s + 1 + i] = lx_text[dv_str + i];
            }
        }
    }
    lx_restore();
    return s;
}

void dt_restore(void)
{
    rd_on = 0;
}

void dt_restore_line(int line)
{
    unsigned int p;
    p = ed_find(line);
    if (line == 0 || LINE_AT(p) != line) { err_arg = line; rt_error(E_NOLINE); return; }
    lx_save();
    if (!is_data(p)) { err_arg = line; rt_error(E_NOTDATA); }
    else if (open_line(p)) {
        rd_on = 1;
        rd_line = p;
        rd_pos = lx_tokpos;
    }
    lx_restore();
}

/* DESIGN: the IDE writes the program into memory between two commands, and
 * nothing tells the interpreter. A hash taken after each command finds the
 * change: any single changed byte always changes the hash.
 *
 * Two changes that cancel each other can still go unseen. A value v and
 * v + D swapped between two bytes d apart collide when 2 divides D and d
 * together at least 11 times. In measured swaps of digits and letters that
 * was about 4 in 1000. The exact fix is a system page byte the IDE bumps
 * when it writes the program. That changes the documented system page, so
 * it is the owner's decision.
 */
static unsigned int prog_hash(void)
{
    unsigned int h;
    unsigned int i;
    unsigned int len;
    len = prog_len;
    h = len;
    for (i = 0; i < len; i++) h = (h << 5) + h + prog[i];
    return h;
}

/* Every change the interpreter makes to the program itself calls dt_restore,
 * which clears rd_on. So if rd_on is still set once a command finishes, the
 * program did not change during it and rd_hash, checked here, is still
 * right: dt_mark has nothing new to hash unless READ has just switched on.
 */
void dt_check(void)
{
    if (rd_on && prog_hash() != rd_hash) rd_on = 0;
    rd_was = rd_on;
}

void dt_mark(void)
{
    if (rd_on && !rd_was) rd_hash = prog_hash();
}
