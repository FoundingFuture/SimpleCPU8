
// build: cc -o basic main.c term.c lex.c expr.c strings.c edit.c run.c bang.c store.c data.c

#include "basic.h"

static char input[LINEMAX];

int main(void)
{
    /* A reset keeps RAM, and with it the 1 the prompt left. */
    at_prompt = 0;
    lx_init();
    term_init();
    str_init();
    ed_new();
    bang_init();
    rt_ink(255);

    term_puts("SimpleCPU-8 BASIC");
    term_nl();
    term_puts("READY");
    term_nl();
    sto_autorun();

    for (;;) {
        term_putc(62);
        /* The IDE reads and writes the program only while this is 1: the
         * program is whole and nothing of BASIC's is half done. AUTORUN,
         * LOAD and a typed line store lines with no program running.
         */
        at_prompt = 1;
        term_readline(input);
        at_prompt = 0;
        rt_line(input);
        if (running == 0) { term_puts("READY"); term_nl(); }
    }
    return 0;
}
