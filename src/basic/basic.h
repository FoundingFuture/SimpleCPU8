
#ifndef BASIC_H
#define BASIC_H

#include <gpu.h>
#include <io.h>
#include <sys.h>
#include <storage.h>

/* The screen is mapped at the top of RAM, and the C stack starts just below
 * it and grows down, so the two never meet.
 *
 * The grid is 42 by 32, which is 1344 bytes, so the buffer starts that far
 * below the top rather than at the round number a 32 column screen used.
 */
#define SCREEN   0xFAC0
#define COLS     42
#define ROWS     32

/* How much room the program text and the string heap get. Both are plain
 * global arrays: this machine has 64K of data RAM and no allocator.
 */
#define PROGMAX  6144
#define HEAPMAX  3072
#define LINEMAX  80

/* A variable is a letter and an optional digit: A, or A0 through A9. */
#define NVARS    286
#define NSTRS    26

/* How deep GOSUB and FOR may nest. Each costs a few bytes of global. */
#define GOSUBMAX 16
#define FORMAX   8

/* Either of these breaks a running program. Ctrl and a letter arrives as a
 * control code, the way a terminal has always delivered it, so Ctrl-C is 3
 * and no modifier has to be looked up to recognise it.
 */
/* The cursor. A solid block at the top of the printable range, which both
 * built-in fonts carry. It used to be 219, which neither font has, so the
 * cursor was invisible and the test that checked it read the RAM byte
 * rather than the screen.
 */
#define CURSOR   0x7F

#define K_BREAK  27
#define K_CTRL_C 3

/* term.c: the screen and the keyboard.
 *
 * Every key in the interpreter comes through key_get, which has a one event
 * pushback. The run loop polls for the break key between lines, and a key
 * that is not the break key goes back so INKEY$ still sees it.
 */
unsigned char key_get(void);
void key_push(unsigned char k);
unsigned char key_break(void);
void term_init(void);
void term_paper(unsigned char c);
void term_cls(void);
void term_putc(unsigned char c);
void term_puts(char *s);
void term_putn(int n);
void term_nl(void);
void term_readline(char *buf);

/* lex.c: one line of source, a token at a time. */
extern char *lx_text;      /* the line being read */
extern unsigned int lx_pos;  /* where the cursor is in it */
extern unsigned char lx_tok;  /* the token just read */
extern int lx_num;         /* its value, when it is a number */
extern char lx_word[12];   /* its text, when it is a name or a keyword */
extern unsigned char lx_len;  /* how long that text is */
extern unsigned int lx_str;  /* heap offset, when it is a string */
extern unsigned int lx_tokpos;  /* where the token last read starts in lx_text */
/* 1 while data.c reads a DATA line. A string then stays in the text:
 * lx_str is the index of its first character and lx_len its length.
 * Nothing goes on the heap, so walking DATA lines leaves no garbage.
 */
extern unsigned char lx_raw;

#define T_END    0
#define T_NUM    1
#define T_NAME   2
#define T_STR    3
#define T_PUNCT  4
#define T_KEY    5

void lx_start(char *text);
void lx_seek(char *text, unsigned int pos);
void lx_next(void);
unsigned char lx_is(char *word);

/* A name is a variable when it is one letter, or a letter and a digit. A
 * trailing dollar makes it a string variable instead. These read lx_len
 * rather than looking past the terminator: lx_word keeps whatever the last
 * token left in it, and PRINT A once read the I of PRINT as part of A.
 */
#define IS_STRNAME (lx_tok == T_NAME && lx_len >= 2 && lx_word[lx_len - 1] == 36)
#define IS_STRVAR (lx_tok == T_NAME && lx_len == 2 && lx_word[1] == 36)
#define IS_INTVAR (lx_tok == T_NAME && (lx_len == 1 || (lx_len == 2 && lx_word[1] >= 48 && lx_word[1] <= 57)))

/* expr.c: an integer expression, and a string one. */
int ex_int(void);
unsigned int ex_str(void);

/* strings.c: the heap and its collector. */
extern unsigned char heap[HEAPMAX];
extern unsigned int svar[NSTRS];

void str_init(void);
unsigned int str_new(unsigned int len);
unsigned int str_from(char *s);
unsigned int str_cat(unsigned int a, unsigned int b);
unsigned char str_len(unsigned int s);
unsigned char str_at(unsigned int s, unsigned int i);
void str_put(unsigned int s);
int str_cmp(unsigned int a, unsigned int b);
void str_collect(void);

/* edit.c: the stored program. */
extern unsigned char prog[PROGMAX];

void ed_new(void);
void ed_store(int line, char *text);
unsigned int ed_find(int line);
void ed_list(unsigned int first, unsigned int last);
/* Number the lines again from start in steps of step, and change the
 * line numbers after GOTO, GOSUB, THEN and RESTORE to match, and inside
 * DATA(. Nothing changes when it cannot finish: E_RENUM past 65535,
 * E_LINELONG, E_MEMORY.
 */
void ed_renum(unsigned int start, unsigned int step);

/* run.c: statements. */
extern int vars[NVARS];
extern unsigned char loop_back;

/* The error codes a program reads back from SYS_ERR. The first twelve
 * keep the numbers they always had. The rest split the old broad ones, so
 * the message can say exactly what went wrong: this BASIC has the room to
 * explain itself, which the old machines did not.
 */
#define E_OK      0
#define E_SYNTAX  1   /* a line not understood; the message says what was expected */
#define E_NOLINE  2   /* GOTO, GOSUB, THEN, RESTORE or DATA( to a line that is not there */
#define E_STACK   3   /* no longer raised: split into 15 to 18 */
#define E_MEMORY  4   /* the program memory is full */
#define E_TYPE    5   /* a string where a number is needed */
#define E_DIVZERO 6
#define E_RANGE   7   /* a number out of range */
#define E_BREAK   8
#define E_BANG    9   /* a ! command no driver knows */
#define E_NOTFOUND 10 /* no program of that name on the cartridge */
#define E_STOFULL 11
#define E_BADNAME 12
#define E_USRWIDTH 13 /* USR's width is not 0, 1 or 2 */
#define E_UNKNOWN 14  /* a word that is not a statement */
#define E_GOSUBS  15  /* GOSUBs nested deeper than GOSUBMAX */
#define E_RETURN  16  /* RETURN with no GOSUB */
#define E_FORS    17  /* FOR loops nested deeper than FORMAX */
#define E_NEXT    18  /* NEXT with no FOR */
#define E_LINELONG 19 /* a program line longer than 250 characters */
#define E_STRLONG 20  /* a string longer than 255 characters */
#define E_STRMEM  21  /* the string memory is full */
#define E_SAVEBIG 22  /* the program is too long to save */
#define E_NEEDSTR 23  /* a number where a string is needed */
#define E_STRCMP  24  /* strings compared with something other than = <> < > <= >= */
#define E_NOTVAR  25  /* a name that is not a variable, assigned or read */
#define E_ROUTINE 26  /* CALL, JMP or USR given a name at the prompt, where only numbers work */
#define E_RENUM   27  /* RENUM would number a line past 65535 */
#define E_NODATA  28  /* READ past the last DATA value */
#define E_NOTDATA 29  /* RESTORE n or DATA(n) of a line that holds no DATA */
#define E_DATABYTE 30 /* a DATA value that does not fit in a byte */

/* CALL, JMP and USR take a routine's slot. A name there works only in a
 * project the builder resolved, so a name that is no variable gets its
 * own message. True when that error was raised.
 */
unsigned char rt_routine_name(void);

void rt_run(void);
void rt_line(char *text);
/* The pen colour INK sets. Every drawing word draws in it. The GPU keeps
 * its own copy for lines and outlines, and PLOT passes this one.
 */
void rt_ink(int c);
void rt_error(unsigned char code);
/* A syntax error that names what should have come. The message then reads
 * EXPECTED what BUT FOUND the token the lexer stands on.
 */
void rt_expect(char *what);
/* The word the message names, when it is not the lexer's token: a !
 * command's word, a program name. Up to the first space.
 */
void rt_found(char *text);
extern int err_arg;   /* a number the message names: the missing line */
int var_slot(void);

/* The system page: the first 32 bytes of the zero page, which the compiler
 * leaves alone (-zp-reserve 32). Every address is fixed and documented in
 * docs/basic-system-page.md, so a program reaches BASIC's state with PEEK,
 * POKE, DEEK and DOKE, and a driver in assembly finds the vector. Words are
 * big-endian, the way DOKE stores them. The old home computers kept their
 * pointers this way, and so does this one.
 *
 * The globals below LIVE here: the macros are the variables.
 */
#define SYS_BANG_VEC    0x00  /* word: the bang handler's instruction slot */
#define SYS_BANG_TEXT   0x02  /* word: the statement's text while a handler runs */
#define SYS_RESULT      0x04  /* byte: the A a bang handler or a JSR routine came back with */
#define SYS_COL         0x05  /* byte: cursor column, 0 to 41 */
#define SYS_ROW         0x06  /* byte: cursor row, 0 to 31 */
#define SYS_KEY         0x07  /* byte: the last key pressed, 0 when none yet */
#define SYS_PROG        0x08  /* word: where the stored program starts */
#define SYS_PROG_LEN    0x0A  /* word: its length in bytes */
#define SYS_VARS        0x0C  /* word: the integer variables, 11 words per letter: A, A0 to A9 */
#define SYS_HEAP        0x0E  /* word: where the string heap starts */
#define SYS_HEAP_TOP    0x10  /* word: bytes of it in use */
#define SYS_ERR         0x12  /* byte: the last error code, 0 for none */
#define SYS_ERR_LINE    0x13  /* word: the line it happened on, -1 in direct mode */
#define SYS_RUNNING     0x15  /* byte: 1 while a program runs */
#define SYS_PC          0x16  /* word: offset of the line being run */
#define SYS_CALL        0x18  /* word: the instruction slot the last JSR or JMP went to */
#define SYS_USR         0x1A  /* 3 words: USR's parameters on the way in, its answer on the way out */
#define SYS_END         0x20  /* the first byte the compiler may use */

/* A word into the page, high byte first, the way DOKE stores one. */
#define doke(a, v)  (poke((a), (unsigned int)(v) >> 8), poke((a) + 1, (unsigned int)(v) & 255))

#define BANG_VEC    SYS_BANG_VEC
#define BANG_TEXT   SYS_BANG_TEXT

#define cx        (*(unsigned char *)SYS_COL)
#define cy        (*(unsigned char *)SYS_ROW)
#define last_key  (*(unsigned char *)SYS_KEY)
#define prog_len  (*(unsigned int *)SYS_PROG_LEN)
#define heap_top  (*(unsigned int *)SYS_HEAP_TOP)
#define err       (*(unsigned char *)SYS_ERR)
#define err_line  (*(int *)SYS_ERR_LINE)
#define running   (*(unsigned char *)SYS_RUNNING)
#define pc        (*(unsigned int *)SYS_PC)

void bang_init(void);
void bang_run(char *text);

/* store.c: the storage driver, the chain's built-in link. */
#define TEXTMAX  8192

unsigned char sto_bang(char *text);
void sto_autorun(void);

/* data.c: DATA lines. RUN places their bytes, DATA(n) finds where a line's
 * bytes go and READ takes the values. docs/basic-data-design.md.
 */
void dt_pack(void);
/* The address line's bytes go to, the same one RUN places them at. Writes
 * nothing, so it answers before a RUN and after an edit.
 */
int dt_addr(int line);
/* The next DATA value for READ, into a number or a string. */
int dt_read_int(void);
unsigned int dt_read_str(void);
/* READ starts again at the first value, or at the first of line n. */
void dt_restore(void);
void dt_restore_line(int line);
/* Around each command at the prompt. A READ part way through a program
 * that changed between two commands starts again at the first value.
 */
void dt_check(void);
void dt_mark(void);

#endif
