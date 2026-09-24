#include "basic.h"

/* The bang statement hands the rest of its line to the routine the vector
 * at BANG_VEC names. The routine gets D1 at the text and answers in A.
 * Zero means the text is not its business. The chain is the drivers' own.
 * A driver keeps the old vector as its next and jumps there for a word it
 * does not know. The last link is the routine below, which claims
 * nothing. The built-in storage driver runs after it, then the error.
 *
 * The compiler cannot call through a pointer, so the call is inline
 * assembly. The C frame pointer is put back from __sp after it.
 */

/* The vector's power-on value. The routine sits inside bang_init because
 * the compiler drops a function nothing calls. The entry jumps over it.
 */
void bang_init(void)
{
    asm("JMP __bang_init_body");
    asm("__bang_pass: LD A <- 0");
    asm("RET");
    asm("__bang_init_body:");
    asm("LD D2 <- &__bang_pass");
    asm("LD [$0000] <- D2");
    /* The pointers a program or a driver reads: where the program, the
     * variables and the heap live. Fixed for the life of the ROM.
     */
    doke(SYS_PROG, (unsigned int)prog);
    doke(SYS_VARS, (unsigned int)vars);
    doke(SYS_HEAP, (unsigned int)heap);
}

static unsigned char bang_call(char *text)
{
    poke(BANG_TEXT, ((unsigned int)text) >> 8);
    poke(BANG_TEXT + 1, ((unsigned int)text) & 255);
    asm("LD D2 <- [$0000]");
    asm("LD D1 <- [$0002]");
    asm("JSR D2");
    asm("LD D1 <- $0004");
    asm("LD [D1] <- A");
    asm("LD D1 <- [__sp]");
    return peek(SYS_RESULT);
}

void bang_run(char *text)
{
    while (*text == 32) text = text + 1;
    if (bang_call(text)) return;
    if (sto_bang(text)) return;
    rt_error(E_BANG);
    rt_found(text);
}
