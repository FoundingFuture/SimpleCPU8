
// build: cc -o basic main.c term.c lex.c expr.c strings.c edit.c run.c bang.c store.c data.c

#include "basic.h"

static char input[LINEMAX];

int main(void)
{
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
    dt_mark();

    for (;;) {
        term_putc(62);
        term_readline(input);
        dt_check();
        rt_line(input);
        dt_mark();
        if (running == 0) { term_puts("READY"); term_nl(); }
    }
    return 0;
}
