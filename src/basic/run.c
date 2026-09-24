
#include "basic.h"

int vars[NVARS];
unsigned char loop_back;

static unsigned int gosub[GOSUBMAX];
static unsigned char ngosub;

/* A FOR remembers the line it is on and where its body starts within that
 * line, so NEXT can go back to the statement after the FOR whether the
 * body shares the FOR's line or follows it.
 */
static unsigned int for_line[FORMAX];
static unsigned int for_pos[FORMAX];
static int for_var[FORMAX];
static int for_to[FORMAX];
static int for_step[FORMAX];
static unsigned char nfor;

/* The offset of the line being run, 0 at the prompt, and where NEXT asked
 * the FOR's line to pick up again.
 */
static unsigned int cur_line;
static unsigned int resume_pos;

static char line_buf[LINEMAX];

void rt_error(unsigned char code)
{
    if (err == E_OK) err = code;
    running = 0;
}

static void say_error(void)
{
    term_puts("? ");
    if (err == E_SYNTAX) term_puts("SYNTAX");
    else if (err == E_NOLINE) term_puts("NO SUCH LINE");
    else if (err == E_STACK) term_puts("TOO DEEP");
    else if (err == E_MEMORY) term_puts("OUT OF MEMORY");
    else if (err == E_TYPE) term_puts("TYPE");
    else if (err == E_DIVZERO) term_puts("DIVIDE BY ZERO");
    else if (err == E_RANGE) term_puts("OUT OF RANGE");
    else if (err == E_BREAK) term_puts("BREAK");
    else if (err == E_BANG) term_puts("UNKNOWN ! COMMAND");
    else if (err == E_NOTFOUND) term_puts("NOT FOUND");
    else if (err == E_STOFULL) term_puts("STORAGE FULL");
    else if (err == E_BADNAME) term_puts("BAD NAME");
    else term_puts("ERROR");
    if (err != E_BREAK) term_puts(" ERROR");
    /* The line, when there is one. Not read back out of pc: the first line
     * of a program sits at offset zero, and testing pc for truth then loses
     * exactly the line a beginner is most likely to be on.
     */
    if (err_line) {
        term_puts(" IN ");
        term_putn(err_line);
    }
    term_nl();
    /* The code stays in SYS_ERR until the next command, so a program can
     * read what went wrong. rt_line clears it on the way in.
     */
}

/* GOTO and GOSUB name a line. Finding it is a walk, and the walk stops at
 * the first line that is not below the target because lines are in order.
 */
static unsigned int line_at(int n)
{
    unsigned int p;
    p = ed_find(n);
    if (((prog[p] << 8) | prog[p + 1]) != n) { rt_error(E_NOLINE); return 0; }
    return p;
}

static void do_print(void)
{
    unsigned char newline;
    newline = 1;
    for (;;) {
        if (lx_tok == T_END) break;
        if (lx_is(":")) break;
        if (lx_is(";")) { newline = 0; lx_next(); continue; }
        if (lx_is(",")) { term_putc(32); newline = 1; lx_next(); continue; }
        newline = 1;
        /* The value is checked before it is printed. An expression that
         * failed comes back as 0, and PRINT 1/0 used to show that 0 in
         * front of the error message.
         */
        if (lx_tok == T_STR || IS_STRNAME) {
            unsigned int s;
            s = ex_str();
            if (err) return;
            str_put(s);
        } else {
            int v;
            v = ex_int();
            if (err) return;
            term_putn(v);
        }
    }
    if (newline) term_nl();
}

static void do_input(void)
{
    unsigned char is_str;
    int slot;
    unsigned char letter;

    if (lx_tok == T_STR) { str_put(lx_str); lx_next(); if (lx_is(";") || lx_is(",")) lx_next(); }
    term_putc(63);
    term_putc(32);
    if (lx_tok != T_NAME) { rt_error(E_SYNTAX); return; }
    is_str = IS_STRVAR;
    letter = lx_word[0] - 65;
    slot = var_slot();
    lx_next();

    term_readline(line_buf);
    if (is_str) {
        svar[letter] = str_from(line_buf);
    } else {
        unsigned int i;
        int v;
        unsigned char neg;
        i = 0;
        v = 0;
        neg = 0;
        if (line_buf[0] == 45) { neg = 1; i = 1; }
        while (line_buf[i] >= 48 && line_buf[i] <= 57) {
            v = v * 10 + (line_buf[i] - 48);
            i = i + 1;
        }
        if (neg) v = -v;
        vars[slot] = v;
    }
}

/* One statement. Returns 0 when the rest of the line is to be skipped, which
 * is what a taken GOTO and an untaken IF both want.
 */
static unsigned char statement(void)
{
    if (lx_tok == T_END) return 1;

    if (lx_is("REM")) { lx_tok = T_END; return 1; }
    /* The bang statement takes the whole rest of the line, colons and all:
     * what a driver makes of its text is the driver's business.
     */
    if (lx_is("!")) { bang_run(&lx_text[lx_pos]); lx_tok = T_END; return 1; }
    if (lx_is("PRINT")) { lx_next(); do_print(); return 1; }
    if (lx_is("CLS")) { lx_next(); term_cls(); return 1; }
    if (lx_is("END") || lx_is("STOP")) { lx_next(); running = 0; return 0; }
    if (lx_is("INPUT")) { lx_next(); do_input(); return 1; }

    if (lx_is("GOTO")) {
        lx_next();
        {
            unsigned int p;
            p = line_at(ex_int());
            if (err) return 0;
            pc = p;
            return 0;
        }
    }

    if (lx_is("GOSUB")) {
        lx_next();
        {
            unsigned int p;
            p = line_at(ex_int());
            if (err) return 0;
            if (ngosub >= GOSUBMAX) { rt_error(E_STACK); return 0; }
            /* The return is the NEXT line: a GOSUB is always the last thing
             * on its line, which is what every small BASIC has assumed.
             */
            /* pc is ALREADY the line after this one: rt_run advances it
             * before the line runs. Adding another record length here
             * skipped a line and read the wrong record's length to do it.
             */
            gosub[ngosub] = pc;
            ngosub = ngosub + 1;
            pc = p;
            return 0;
        }
    }

    if (lx_is("RETURN")) {
        lx_next();
        if (ngosub == 0) { rt_error(E_STACK); return 0; }
        ngosub = ngosub - 1;
        pc = gosub[ngosub];
        return 0;
    }

    if (lx_is("IF")) {
        lx_next();
        {
            int cond;
            cond = ex_int();
            if (err) return 0;
            if (lx_is("THEN")) lx_next();
            if (cond == 0) { lx_tok = T_END; return 1; }
            /* THEN 100 is a GOTO, which is how BASIC has always read. */
            if (lx_tok == T_NUM) {
                unsigned int p;
                p = line_at(lx_num);
                if (err) return 0;
                pc = p;
                return 0;
            }
            return statement();
        }
    }

    if (lx_is("FOR")) {
        lx_next();
        {
            int slot;
            int from;
            if (lx_tok != T_NAME) { rt_error(E_SYNTAX); return 1; }
            slot = var_slot();
            lx_next();
            if (lx_is("=")) lx_next(); else { rt_error(E_SYNTAX); return 1; }
            from = ex_int();
            vars[slot] = from;
            if (lx_is("TO")) lx_next(); else { rt_error(E_SYNTAX); return 1; }
            if (nfor >= FORMAX) { rt_error(E_STACK); return 1; }
            for_var[nfor] = slot;
            for_to[nfor] = ex_int();
            for_step[nfor] = 1;
            if (lx_is("STEP")) { lx_next(); for_step[nfor] = ex_int(); }
            /* The body starts at the token after the FOR. lx_pos is past
             * it already, so seeking there reads the body's first token.
             */
            for_line[nfor] = cur_line;
            for_pos[nfor] = lx_pos;
            nfor = nfor + 1;
            return 1;
        }
    }

    if (lx_is("NEXT")) {
        lx_next();
        if (lx_tok == T_NAME) lx_next();   /* NEXT I, and the name is ignored */
        if (nfor == 0) { rt_error(E_STACK); return 1; }
        {
            unsigned char t;
            int v;
            t = nfor - 1;
            v = vars[for_var[t]] + for_step[t];
            vars[for_var[t]] = v;
            if (for_step[t] >= 0 ? v <= for_to[t] : v >= for_to[t]) {
                /* Back to the statement after the FOR. On this line the
                 * body starts again in place, on another line rt_run
                 * reads that line from the body's first token.
                 */
                resume_pos = for_pos[t];
                if (for_line[t] == cur_line) return 3;
                pc = for_line[t];
                loop_back = 1;
                return 2;
            }
            nfor = nfor - 1;
            return 1;
        }
    }

    if (lx_is("LET")) lx_next();

    /* Graphics, so a program can draw. Each is one GPU command. */
    if (lx_is("COLOR")) { lx_next(); gpu_set_color(ex_int()); return 1; }
    if (lx_is("PLOT")) {
        lx_next();
        {
            int x;
            int y;
            x = ex_int();
            if (lx_is(",")) lx_next();
            y = ex_int();
            gpu_plot(x >> 8, x, y >> 8, y, 255);
            return 1;
        }
    }
    if (lx_is("MOVE")) {
        lx_next();
        {
            int x;
            int y;
            x = ex_int();
            if (lx_is(",")) lx_next();
            y = ex_int();
            gpu_move_to(x >> 8, x, y >> 8, y);
            return 1;
        }
    }
    if (lx_is("DRAW")) {
        lx_next();
        {
            int x;
            int y;
            x = ex_int();
            if (lx_is(",")) lx_next();
            y = ex_int();
            gpu_line_to(x >> 8, x, y >> 8, y);
            return 1;
        }
    }
    if (lx_is("CIRCLE")) { lx_next(); gpu_circle(ex_int()); return 1; }
    if (lx_is("PAPER")) { lx_next(); term_paper(ex_int()); return 1; }
    if (lx_is("WAIT")) {
        lx_next();
        {
            int n;
            n = ex_int();
            while (n > 0) { wait_frame(); n = n - 1; }
            return 1;
        }
    }
    if (lx_is("POKE")) {
        lx_next();
        {
            int a;
            int v;
            a = ex_int();
            if (lx_is(",")) lx_next();
            v = ex_int();
            poke(a, v);
            return 1;
        }
    }
    /* CALL n calls the routine at instruction slot n and parks the A it
     * came back with at SYS_RESULT, the way the bang dispatcher does, so
     * PEEK(4) reads it. JSR is the same word, spelled the way the machine
     * spells it. JMP n goes there and never comes back. The slot is
     * parked at SYS_CALL first, because inline assembly cannot name a C
     * local. The frame pointer is put back from __sp after the call, as
     * bang.c does. A name in place of the number is what a project build
     * resolves to a slot before the program reaches the ROM. Here it is a
     * syntax error, the way any unknown word in an expression is.
     */
    if (lx_is("CALL") || lx_is("JSR")) {
        lx_next();
        {
            int n;
            n = ex_int();
            if (err) return 1;
            doke(SYS_CALL, n);
            asm("LD D2 <- [$0018]");
            asm("JSR D2");
            asm("LD D1 <- $0004");
            asm("LD [D1] <- A");
            asm("LD D1 <- [__sp]");
            return 1;
        }
    }
    if (lx_is("JMP")) {
        lx_next();
        {
            int n;
            n = ex_int();
            if (err) return 1;
            doke(SYS_CALL, n);
            asm("LD D2 <- [$0018]");
            asm("JMP D2");
            return 1;
        }
    }
    /* A word, high byte first, the way every word on this machine is. */
    if (lx_is("DOKE")) {
        lx_next();
        {
            int a;
            int v;
            a = ex_int();
            if (lx_is(",")) lx_next();
            v = ex_int();
            poke(a, v >> 8);
            poke(a + 1, v);
            return 1;
        }
    }

    /* Anything else has to be an assignment. */
    if (lx_tok == T_NAME) {
        unsigned char is_str;
        int slot;
        unsigned char letter;
        is_str = IS_STRVAR;
        letter = lx_word[0] - 65;
        slot = var_slot();
        lx_next();
        if (!lx_is("=")) { rt_error(E_SYNTAX); return 1; }
        lx_next();
        if (is_str) svar[letter] = ex_str();
        else vars[slot] = ex_int();
        return 1;
    }

    rt_error(E_SYNTAX);
    return 1;
}

/* Every statement on one line, separated by colons, from a position in the
 * text. That is 0 for a fresh line, and the body's first token when NEXT
 * sent the program back to the line its FOR is on. The FOR itself is not
 * run again: re-running it would reset the counter for ever.
 */
static void run_line(char *text, unsigned int pos)
{
    unsigned char r;
    lx_seek(text, pos);
    for (;;) {
        if (lx_tok == T_END) return;
        r = statement();
        if (err || running == 0) return;
        if (r == 0) return;         /* the statement moved pc itself */
        if (r == 2) return;         /* NEXT jumped back to the FOR's line */
        if (r == 3) {               /* NEXT went back to a FOR on this line */
            /* The break check between lines never comes round for a loop
             * that lives on one line, so it is made here instead.
             */
            if (key_break()) { rt_error(E_BREAK); return; }
            lx_seek(text, resume_pos);
            continue;
        }
        if (lx_is(":")) { lx_next(); continue; }
        if (lx_tok == T_END) return;
        rt_error(E_SYNTAX);
        return;
    }
}

void rt_run(void)
{
    unsigned char from_for;
    ngosub = 0;
    nfor = 0;
    err = E_OK;
    err_line = 0;
    running = 1;
    pc = 0;
    from_for = 0;
    loop_back = 0;

    while (running) {
        unsigned int here;
        if ((prog[pc] == 0) && (prog[pc + 1] == 0)) break;
        here = pc;
        err_line = (prog[pc] << 8) | prog[pc + 1];
        /* Escape or Ctrl-C stops the program, and says which line it was on. */
        if (key_break()) {
            rt_error(E_BREAK);
            say_error();
            return;
        }
        pc = pc + prog[pc + 2];       /* the default next line */
        cur_line = here;
        loop_back = 0;
        if (from_for) {
            from_for = 0;
            run_line((char *)&prog[here + 3], resume_pos);
        } else {
            run_line((char *)&prog[here + 3], 0);
        }
        if (err) { say_error(); return; }
        /* NEXT says so itself rather than being guessed at from pc: a GOTO
         * back to the line a FOR is on looks identical otherwise.
         */
        if (loop_back) from_for = 1;
    }
    running = 0;
    err_line = 0;
}

/* A line typed at the prompt. A number in front stores it, anything else
 * runs now, which is what makes BASIC feel like BASIC.
 */
void rt_line(char *text)
{
    unsigned int i;
    int n;

    /* A new command starts clean. The previous error stayed readable in
     * SYS_ERR until now.
     */
    err = E_OK;
    err_line = 0;
    i = 0;
    while (text[i] == 32) i = i + 1;
    if (text[i] >= 48 && text[i] <= 57) {
        n = 0;
        while (text[i] >= 48 && text[i] <= 57) {
            n = n * 10 + (text[i] - 48);
            i = i + 1;
        }
        while (text[i] == 32) i = i + 1;
        ed_store(n, &text[i]);
        if (err) say_error();
        return;
    }

    lx_start(&text[i]);
    if (lx_is("RUN")) { rt_run(); return; }
    if (lx_is("LIST")) { ed_list(); return; }
    if (lx_is("NEW")) { ed_new(); str_init(); term_puts("READY"); term_nl(); return; }

    /* A loop typed at the prompt lives on that one line, and a FOR a
     * program left open has no line to go back to now.
     */
    running = 1;
    pc = 0;
    cur_line = 0;
    nfor = 0;
    run_line(&text[i], 0);
    running = 0;
    if (err) say_error();
}
