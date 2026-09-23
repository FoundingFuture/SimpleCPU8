
#include "basic.h"


/* One key of pushback. The run loop looks at every key for a break, and a
 * key that is not one has to go back, or a program polling INKEY$ would lose
 * every second keystroke to the check.
 */
static unsigned char pushed;

void key_push(unsigned char k)
{
    pushed = k;
}

unsigned char key_get(void)
{
    unsigned char k;
    if (pushed) { k = pushed; pushed = 0; return k; }
    k = io_key();
    if (k && io_key_is_up(k) == 0) last_key = io_key_code(k);
    return k;
}

/* Did a break key just arrive? Escape, or Ctrl-C, which a terminal has
 * always delivered as code 3.
 *
 * This reads the DEVICE and never the pushback. Reading through key_get
 * meant the check kept finding its own pushed key: the release event left
 * over from the Enter that started the program came back round for ever,
 * no new key was ever examined, and Escape sat unread behind it. A program
 * that polled INKEY$ hid the jam by consuming the pushback each pass.
 *
 * A release is dropped rather than pushed back. BASIC reads presses, so a
 * release is noise, and noise in a one deep pushback is what jammed it.
 */
unsigned char key_break(void)
{
    unsigned char k;
    unsigned char c;

    k = io_key();
    if (k == 0) return 0;
    if (io_key_is_up(k)) return 0;
    c = io_key_code(k);
    if (c == K_BREAK || c == K_CTRL_C) return 1;
    pushed = k;
    return 0;
}

void term_init(void)
{
    /* Text mode reads its screen straight out of data RAM, so writing a
     * character is a store and the GPU shows it on the next frame.
     */
    gpu_set_textmode(SCREEN >> 8, SCREEN & 255);
    term_cls();

    /* The key buffer is hardware and survives a program load, which is
     * deliberate. So whatever was typed at the LAST program is still queued,
     * and BASIC would read it as its first command. Throw it away: a fresh
     * prompt should be waiting for this person, not for the previous one.
     */
    pushed = 0;
    while (io_key()) { }
}

void term_cls(void)
{
    unsigned int i;
    for (i = 0; i < COLS * ROWS; i++) poke(SCREEN + i, 32);
    cx = 0;
    cy = 0;
}

static void scroll(void)
{
    unsigned int i;
    /* One command moves the whole screen up a row. The CPU has no block
     * move, so without it this would be 992 loads and 992 stores.
     */
    memmove(SCREEN, SCREEN + COLS, (ROWS - 1) * COLS);
    for (i = 0; i < COLS; i++) poke(SCREEN + (ROWS - 1) * COLS + i, 32);
}

void term_nl(void)
{
    cx = 0;
    cy = cy + 1;
    if (cy >= ROWS) {
        scroll();
        cy = ROWS - 1;
    }
}

void term_putc(unsigned char c)
{
    if (c == 10) { term_nl(); return; }
    poke(SCREEN + cy * COLS + cx, c);
    cx = cx + 1;
    if (cx >= COLS) term_nl();
}

void term_puts(char *s)
{
    while (*s) { term_putc(*s); s = s + 1; }
}

void term_putn(int n)
{
    unsigned char buf[8];
    unsigned char i;
    unsigned int u;

    if (n < 0) { term_putc(45); u = -n; } else u = n;
    i = 0;
    if (u == 0) { term_putc(48); return; }
    while (u) {
        buf[i] = 48 + (u % 10);
        u = u / 10;
        i = i + 1;
    }
    while (i) { i = i - 1; term_putc(buf[i]); }
}

/* Read a line, echoing it, until Enter. Backspace rubs one out. The cursor
 * is drawn as a solid block and taken away again, so a person can see where
 * they are typing.
 */
void term_readline(char *buf)
{
    unsigned char n;
    unsigned char k;

    n = 0;
    for (;;) {
        poke(SCREEN + cy * COLS + cx, CURSOR);
        k = key_get();
        if (k == 0) continue;
        if (io_key_is_up(k)) continue;
        k = io_key_code(k);

        if (k == 13) {
            poke(SCREEN + cy * COLS + cx, 32);
            buf[n] = 0;
            term_nl();
            return;
        }
        if (k == 8) {
            if (n) {
                poke(SCREEN + cy * COLS + cx, 32);
                n = n - 1;
                if (cx) cx = cx - 1;
                poke(SCREEN + cy * COLS + cx, 32);
            }
            continue;
        }
        if (k < 32) continue;
        if (n >= LINEMAX - 1) continue;
        buf[n] = k;
        n = n + 1;
        term_putc(k);
    }
}
