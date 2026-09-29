
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

/* The jump cache. A line number after GOTO, GOSUB or THEN is a constant,
 * and no statement adds or removes a line while a program runs. So the
 * record a jump found once stays its target until the program stops.
 * docs/design/basic-speed.md, proposal 1.
 *
 * A slot holds a site and a target. The site is the offset in prog where
 * the jump's line number starts, the target the offset line_at found. A
 * site picks its slot by the XOR of its two bytes. Two sites that share a
 * slot take turns in it and stay correct, since the site is compared on
 * every read. A line's text starts at offset 3, so no site is 0, and 0
 * marks an empty slot.
 *
 * DESIGN: the cache lives only while rt_run runs, and rt_run empties it on
 * the way in. A line typed at the prompt never reads or writes it: its
 * number sits in the input buffer, where an offset names no place in the
 * program. So an edit, NEW, RENUM or LOAD needs no step of its own to
 * empty it. The next RUN does. A POKE into the program text while it runs
 * is not supported, docs/guides/basic.md.
 */
#define JCMAX 256
static unsigned int jc_site[JCMAX];
static unsigned int jc_target[JCMAX];
static unsigned char jc_on;
static unsigned char jc_slot;
static unsigned int jc_at;
static unsigned int jc_found;

/* The site of the number after the lexer's token, into jc_at and jc_slot.
 * 1 when the slot holds its target, which jc_found then holds, and the
 * digits are never read. jc_at is 0 when there is no site to keep.
 */
static unsigned char jc_hit(void)
{
    char *t;
    jc_at = 0;
    if (jc_on == 0) return 0;
    /* A pointer that walks reads a byte in two instructions, where
     * lx_text[i] adds two words first.
     */
    t = lx_text + lx_pos;
    while (*t == 32) t = t + 1;
    /* THEN B = 1 has no number to look up. A $ starts a hex one. */
    if ((*t < 48 || *t > 57) && *t != 36) return 0;
    jc_at = (unsigned int)t - (unsigned int)prog;
    jc_slot = jc_at ^ (jc_at >> 8);
    if (jc_site[jc_slot] != jc_at) return 0;
    jc_found = jc_target[jc_slot];
    return 1;
}

/* Keep the target a jump found, at the site jc_hit noted. */
static void jc_put(unsigned int target)
{
    if (jc_at == 0) return;
    jc_site[jc_slot] = jc_at;
    jc_target[jc_slot] = target;
}

/* True when the lexer stands on a number that is the whole operand: the
 * end of the line or a colon follows it. GOTO N or GOTO 10 * N is worked
 * out on every run and never kept.
 */
static unsigned char lone_number(void)
{
    unsigned int q;
    if (lx_tok != T_NUM) return 0;
    q = lx_pos;
    while (lx_text[q] == 32) q = q + 1;
    return lx_text[q] == 0 || lx_text[q] == 58;
}

/* What the first error of a command was about, for the message. err_what
 * is what a syntax error expected, err_found what stood there instead.
 */
static char *err_what;
static char err_found[24];
int err_arg;
static int ink;

void rt_ink(int c)
{
    ink = c & 255;
    gpu_set_color(ink);
}

static void rt_found_at(unsigned char at, char *text)
{
    unsigned char i;
    i = 0;
    while (text[i] && text[i] != 32 && at + i < 23) { err_found[at + i] = text[i]; i = i + 1; }
    err_found[at + i] = 0;
}

void rt_found(char *text) { rt_found_at(0, text); }

/* The token the lexer stands on, in words. */
static void note_found(void)
{
    char digits[7];
    unsigned int n;
    unsigned char d;
    unsigned char i;
    if (lx_tok == T_END) { err_found[0] = 0; return; }
    if (lx_tok == T_STR) {
        rt_found("A");
        err_found[1] = 32;
        rt_found_at(2, "STRING");
        return;
    }
    if (lx_tok == T_NUM) {
        n = lx_num;
        d = 0;
        do { digits[d] = 48 + (n % 10); n = n / 10; d = d + 1; } while (n);
        for (i = 0; i < d; i++) err_found[i] = digits[d - 1 - i];
        err_found[d] = 0;
        return;
    }
    rt_found(lx_word);
}

void rt_error(unsigned char code)
{
    if (err == E_OK) {
        err = code;
        err_what = 0;
        note_found();
    }
    running = 0;
}

unsigned char rt_routine_name(void)
{
    if (lx_tok != T_NAME || IS_INTVAR) return 0;
    rt_error(E_ROUTINE);
    return 1;
}

void rt_expect(char *what)
{
    if (err == E_OK) {
        rt_error(E_SYNTAX);
        err_what = what;
    }
    running = 0;
}

/* LIST, LIST 100, LIST 100-200, LIST -200 and LIST 100-: the whole
 * program, one line, or a range with either end left open.
 */
static void do_list(void)
{
    unsigned int first;
    unsigned int last;
    first = 1;
    last = 65535;
    lx_next();
    if (lx_tok == T_NUM) {
        first = lx_num;
        last = lx_num;
        lx_next();
        if (lx_is("-")) {
            last = 65535;
            lx_next();
            if (lx_tok == T_NUM) { last = lx_num; lx_next(); }
        }
    } else if (lx_is("-")) {
        lx_next();
        if (lx_tok != T_NUM) { rt_expect("A LINE NUMBER AFTER -"); return; }
        last = lx_num;
        lx_next();
    }
    if (lx_tok != T_END) {
        if (first == 1 && last == 65535) rt_expect("A LINE NUMBER");
        else rt_expect("THE END OF THE LINE");
        return;
    }
    ed_list(first, last);
}

static void say_error(void)
{
    term_puts("? ");
    if (err == E_SYNTAX) term_puts("SYNTAX ERROR");
    else if (err == E_NOLINE) { term_puts("THERE IS NO LINE "); term_putn(err_arg); }
    else if (err == E_MEMORY) term_puts("THE PROGRAM MEMORY IS FULL: 6144 BYTES AT MOST");
    else if (err == E_TYPE) term_puts("A STRING CANNOT BE USED AS A NUMBER");
    else if (err == E_DIVZERO) term_puts("DIVISION BY ZERO");
    else if (err == E_RANGE) term_puts("NUMBER OUT OF RANGE");
    else if (err == E_BREAK) term_puts("BREAK");
    else if (err == E_BANG) { term_puts("NO DRIVER KNOWS THE COMMAND !"); term_puts(err_found); }
    else if (err == E_NOTFOUND) { term_puts("NO PROGRAM CALLED "); term_puts(err_found); term_puts(" ON THE CARTRIDGE"); }
    else if (err == E_STOFULL) term_puts("THE CARTRIDGE IS FULL");
    else if (err == E_BADNAME) term_puts("A PROGRAM NAME IS 1 TO 16 CHARACTERS WITHOUT SPACES");
    else if (err == E_USRWIDTH) term_puts("RETURN VALUE WIDTH IS OUT OF RANGE [0,1,2]");
    else if (err == E_UNKNOWN) { term_puts("UNKNOWN WORD "); term_puts(err_found); }
    else if (err == E_GOSUBS) term_puts("TOO MANY GOSUBS INSIDE EACH OTHER: 16 AT MOST");
    else if (err == E_RETURN) term_puts("RETURN WITHOUT A GOSUB");
    else if (err == E_FORS) term_puts("TOO MANY FOR LOOPS INSIDE EACH OTHER: 8 AT MOST");
    else if (err == E_NEXT) term_puts("NEXT WITHOUT A FOR");
    else if (err == E_LINELONG) term_puts("THE LINE IS TOO LONG: 250 CHARACTERS AT MOST");
    else if (err == E_STRLONG) term_puts("THE STRING IS TOO LONG: 255 CHARACTERS AT MOST");
    else if (err == E_STRMEM) term_puts("OUT OF MEMORY FOR STRINGS");
    else if (err == E_SAVEBIG) term_puts("THE PROGRAM IS TOO LONG TO SAVE");
    else if (err == E_NEEDSTR) term_puts("A NUMBER CANNOT BE USED AS A STRING");
    else if (err == E_STRCMP) term_puts("STRINGS ARE COMPARED WITH = <> < > <= OR >=");
    else if (err == E_RENUM) term_puts("RENUM WOULD NUMBER A LINE PAST 65535");
    else if (err == E_NODATA) term_puts("READ FOUND NO MORE DATA");
    else if (err == E_NOTDATA) { term_puts("LINE "); term_putn(err_arg); term_puts(" HOLDS NO DATA"); }
    else if (err == E_DATABYTE) term_puts("A DATA VALUE IS ONE BYTE: -128 TO 255");
    else if (err == E_HEXWIDTH) term_puts("HEX WIDTH IS OUT OF RANGE [1,4]");
    else if (err == E_TEXTSIZE) term_puts("TEXT SIZE IS OUT OF RANGE [4,8]");
    else if (err == E_NOFONT) { term_puts("NO FONT CALLED "); term_puts(err_found); term_puts(" ON THE CARTRIDGE"); }
    else if (err == E_ROUTINE) {
        term_puts(err_found);
        term_puts(" IS A ROUTINE NAME, WHICH ONLY A BUILT PROJECT KNOWS: USE ITS NUMBER HERE");
    }
    else if (err == E_NOTVAR) {
        term_puts(err_found);
        term_puts(" IS NOT A VARIABLE: A VARIABLE IS ONE LETTER, OR A LETTER AND A DIGIT");
    }
    else term_puts("ERROR");
    /* The line, when there is one. Not read back out of pc: the first line
     * of a program sits at offset zero, and testing pc for truth then loses
     * exactly the line a beginner is most likely to be on.
     */
    if (err_line) {
        term_puts(" IN LINE ");
        term_putn(err_line);
    }
    /* A syntax error says what it wanted and what it got. */
    if (err == E_SYNTAX) {
        term_puts(": ");
        if (err_what) { term_puts("EXPECTED "); term_puts(err_what); term_puts(" BUT FOUND "); }
        else term_puts("DID NOT EXPECT ");
        if (err_found[0]) term_puts(err_found);
        else term_puts("THE END OF THE LINE");
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
    if (((prog[p] << 8) | prog[p + 1]) != n) { err_arg = n; rt_error(E_NOLINE); return 0; }
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
    if (lx_tok != T_NAME) { rt_expect("A VARIABLE AFTER INPUT"); return; }
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
    /* RUN, DATA(n) and READ read a DATA line. Running it does nothing.
     * DATA must open its line, because that is where they look for it.
     */
    if (lx_is("DATA")) {
        if (lx_tokpos) { rt_expect("A STATEMENT"); return 1; }
        lx_tok = T_END;
        return 1;
    }
    if (lx_is("PRINT")) { lx_next(); do_print(); return 1; }
    if (lx_is("CLS")) { lx_next(); term_cls(); return 1; }
    if (lx_is("END") || lx_is("STOP")) { lx_next(); running = 0; return 0; }
    if (lx_is("INPUT")) { lx_next(); do_input(); return 1; }

    /* READ A, B$: the next DATA values, across lines. */
    if (lx_is("READ")) {
        lx_next();
        for (;;) {
            if (lx_tok != T_NAME) { rt_expect("A VARIABLE AFTER READ"); return 1; }
            if (IS_STRVAR) {
                unsigned char letter;
                unsigned int s;
                letter = lx_word[0] - 65;
                s = dt_read_str();
                if (err) return 1;
                svar[letter] = s;
            } else if (IS_INTVAR) {
                int slot;
                int v;
                slot = var_slot();
                v = dt_read_int();
                if (err) return 1;
                vars[slot] = v;
            } else {
                rt_error(E_NOTVAR);
                return 1;
            }
            lx_next();
            if (!lx_is(",")) return 1;
            lx_next();
        }
    }

    if (lx_is("RESTORE")) {
        lx_next();
        if (lx_tok == T_END || lx_is(":")) { dt_restore(); return 1; }
        {
            int n;
            n = ex_int();
            if (err) return 1;
            dt_restore_line(n);
            return 1;
        }
    }

    if (lx_is("GOTO")) {
        if (jc_hit()) { pc = jc_found; return 0; }
        lx_next();
        {
            unsigned int p;
            unsigned char lone;
            lone = lone_number();
            p = line_at(ex_int());
            if (err) return 0;
            if (lone) jc_put(p);
            pc = p;
            return 0;
        }
    }

    if (lx_is("GOSUB")) {
        {
            unsigned int p;
            unsigned char lone;
            if (jc_hit()) {
                p = jc_found;
                lone = 0;
            } else {
                lx_next();
                lone = lone_number();
                p = line_at(ex_int());
                if (err) return 0;
            }
            if (ngosub >= GOSUBMAX) { rt_error(E_GOSUBS); return 0; }
            if (lone) jc_put(p);
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
        if (ngosub == 0) { rt_error(E_RETURN); return 0; }
        ngosub = ngosub - 1;
        pc = gosub[ngosub];
        return 0;
    }

    if (lx_is("IF")) {
        lx_next();
        {
            int cond;
            unsigned char then;
            cond = ex_int();
            if (err) return 0;
            then = 0;
            if (lx_is("THEN")) {
                /* THEN 100 takes the number alone, so its target is kept
                 * whatever follows it.
                 */
                if (cond != 0 && jc_hit()) { pc = jc_found; return 0; }
                then = cond != 0;
                lx_next();
            }
            if (cond == 0) { lx_tok = T_END; return 1; }
            /* THEN 100 is a GOTO, which is how BASIC has always read. */
            if (lx_tok == T_NUM) {
                unsigned int p;
                p = line_at(lx_num);
                if (err) return 0;
                if (then) jc_put(p);
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
            if (lx_tok != T_NAME) { rt_expect("A VARIABLE AFTER FOR"); return 1; }
            if (!IS_INTVAR) { rt_error(E_NOTVAR); return 1; }
            slot = var_slot();
            lx_next();
            if (lx_is("=")) lx_next(); else { rt_expect("="); return 1; }
            from = ex_int();
            vars[slot] = from;
            if (lx_is("TO")) lx_next(); else { rt_expect("TO"); return 1; }
            if (nfor >= FORMAX) { rt_error(E_FORS); return 1; }
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
        if (nfor == 0) { rt_error(E_NEXT); return 1; }
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

    /* Graphics, so a program can draw. Each is one GPU command, or two
     * for a filled CIRCLE. All draw in the INK colour.
     */
    if (lx_is("INK")) { lx_next(); rt_ink(ex_int()); return 1; }
    if (lx_is("PLOT")) {
        lx_next();
        {
            int x;
            int y;
            x = ex_int();
            if (lx_is(",")) lx_next();
            y = ex_int();
            gpu_plot(x >> 8, x, y >> 8, y, ink);
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
    /* CIRCLE rx, ry, fill: an outline around the pen in INK. ry is
     * optional and makes an ellipse, fill is optional and fills the inside
     * first. Brackets around the three are allowed, as for a function.
     */
    if (lx_is("CIRCLE")) {
        lx_next();
        {
            int rx;
            int ry;
            int fill;
            unsigned char bracket;
            unsigned char filled;
            bracket = 0;
            filled = 0;
            fill = 0;
            if (lx_is("(")) { lx_next(); bracket = 1; }
            rx = ex_int();
            ry = rx;
            if (lx_is(",")) { lx_next(); ry = ex_int(); }
            if (lx_is(",")) { lx_next(); fill = ex_int(); filled = 1; }
            if (bracket) {
                if (!lx_is(")")) { rt_expect(")"); return 1; }
                lx_next();
            }
            if (filled) {
                gpu_set_color(fill);
                out(GPU_RADIUS_Y, ry);
                gpu_circle(rx);
                gpu_set_color(ink);
            }
            out(GPU_RADIUS_Y, ry);
            gpu_ring(rx);
            return 1;
        }
    }
    if (lx_is("PAPER")) { lx_next(); term_paper(ex_int()); return 1; }
    /* LOADFONT name: the font of that name on the cartridge, which the
     * storage device finds in any case. LOADFONT alone: the built-in font.
     * The grid follows the font's cell, and the screen starts over.
     */
    if (lx_is("LOADFONT")) {
        lx_next();
        if (lx_tok == T_END || lx_is(":")) { gpu_reset_font(); term_grid(); return 1; }
        if (lx_tok != T_NAME) { rt_expect("A FONT NAME AFTER LOADFONT"); return 1; }
        {
            /* STO_FIND's answer: a kind, three bytes of cartridge address,
             * three of length. A local, so the zero page keeps what the
             * interpreter's loops use.
             */
            unsigned char found[7];
            sto_find(found, lx_word);
            if (sto_status() != STO_OK || found[0] != STO_KIND_FONT) { rt_error(E_NOFONT); return 1; }
            gpu_load_font(found[1], found[2], found[3]);
        }
        lx_next();
        term_grid();
        return 1;
    }
    /* SETTEXT w, h: the cell in pixels, 4 to 8 each, checked before the GPU
     * hears of it. A size the GPU refuses would stop the machine.
     */
    if (lx_is("SETTEXT")) {
        lx_next();
        {
            int w;
            int h;
            w = ex_int();
            if (!lx_is(",")) { rt_expect(", AND THE HEIGHT"); return 1; }
            lx_next();
            h = ex_int();
            if (err) return 1;
            if (w < 4 || w > 8 || h < 4 || h > 8) { rt_error(E_TEXTSIZE); return 1; }
            gpu_text_cell(w, h);
            term_grid();
            return 1;
        }
    }
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
            a = ex_int();
            if (lx_is(",")) lx_next();
            poke(a, ex_int());
            /* POKE a, 1, 2, 3 fills the boxes after a in turn. */
            while (lx_is(",")) {
                lx_next();
                a = a + 1;
                poke(a, ex_int());
            }
            return 1;
        }
    }
    /* CALL n calls the routine at instruction slot n and parks the A it
     * came back with at SYS_RESULT, the way the bang dispatcher does, so
     * PEEK(4) reads it. JSR is the same word, spelled the way the machine
     * spells it. JMP n goes there and never comes back. The slot is
     * parked at SYS_CALL first, because inline assembly cannot name a C
     * local. The routine keeps D3, the C frame pointer, as bang.c says.
     * A name in place of the number is what a project build resolves to a
     * slot before the program reaches the ROM. Here it is a syntax error,
     * the way any unknown word in an expression is.
     */
    if (lx_is("CALL") || lx_is("JSR")) {
        lx_next();
        if (rt_routine_name()) return 1;
        {
            int n;
            n = ex_int();
            if (err) return 1;
            doke(SYS_CALL, n);
            asm("LD D2 <- [$0018]");
            asm("JSR D2");
            asm("LD D1 <- $0004");
            asm("LD [D1] <- A");
            return 1;
        }
    }
    if (lx_is("JMP")) {
        lx_next();
        if (rt_routine_name()) return 1;
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

    /* Anything else has to be an assignment. A name that is not a
     * variable is a statement mistyped, or a variable name too long, and
     * the = after it tells which.
     */
    if (lx_tok == T_NAME) {
        unsigned char is_str;
        int slot;
        unsigned char letter;
        char name[12];
        unsigned char valid;
        unsigned char i;
        valid = IS_INTVAR || IS_STRVAR;
        for (i = 0; i < 11 && lx_word[i]; i++) name[i] = lx_word[i];
        name[i] = 0;
        is_str = IS_STRVAR;
        letter = lx_word[0] - 65;
        slot = var_slot();
        lx_next();
        if (!valid) {
            rt_error(lx_is("=") ? E_NOTVAR : E_UNKNOWN);
            rt_found(name);
            return 1;
        }
        if (!lx_is("=")) { rt_expect("= AFTER THE VARIABLE"); return 1; }
        lx_next();
        if (is_str) svar[letter] = ex_str();
        else vars[slot] = ex_int();
        return 1;
    }

    rt_expect("A STATEMENT");
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
        rt_expect(": OR THE END OF THE LINE");
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

    /* The jump cache starts empty on every RUN. */
    memset(jc_site, 0, sizeof(jc_site));
    jc_on = 1;

    /* The DATA lines' bytes go where they belong before line 1 runs. */
    dt_pack();
    if (err) { say_error(); running = 0; return; }
    err_line = 0;

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
    if (lx_is("LIST")) { do_list(); if (err) say_error(); return; }
    if (lx_is("NEW")) { ed_new(); str_init(); term_puts("READY"); term_nl(); return; }
    /* RENUM start, step: 10 and 10 unless given. */
    if (lx_is("RENUM")) {
        unsigned int start;
        unsigned int step;
        lx_next();
        start = 10;
        step = 10;
        if (lx_tok != T_END) {
            start = ex_int();
            if (lx_is(",")) { lx_next(); step = ex_int(); }
        }
        if (!err && (start == 0 || step == 0)) rt_error(E_RANGE);
        if (!err) ed_renum(start, step);
        if (err) say_error();
        return;
    }

    /* A loop typed at the prompt lives on that one line, and a FOR a
     * program left open has no line to go back to now.
     */
    running = 1;
    pc = 0;
    cur_line = 0;
    nfor = 0;
    jc_on = 0;
    run_line(&text[i], 0);
    running = 0;
    if (err) say_error();
}
