
#ifndef BASIC_H
#define BASIC_H

#include <gpu.h>
#include <io.h>
#include <sys.h>

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

#define T_END    0
#define T_NUM    1
#define T_NAME   2
#define T_STR    3
#define T_PUNCT  4
#define T_KEY    5

void lx_start(char *text);
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
extern unsigned int heap_top;
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
extern unsigned int prog_len;

void ed_new(void);
void ed_store(int line, char *text);
unsigned int ed_find(int line);
void ed_list(void);

/* run.c: statements. */
extern int vars[NVARS];
extern unsigned char running;
extern unsigned int pc;        /* offset of the line being run */
extern unsigned char err;
extern int err_line;
extern unsigned char loop_back;

#define E_OK      0
#define E_SYNTAX  1
#define E_NOLINE  2
#define E_STACK   3
#define E_MEMORY  4
#define E_TYPE    5
#define E_DIVZERO 6
#define E_RANGE   7
#define E_BREAK   8

void rt_run(void);
void rt_line(char *text);
void rt_error(unsigned char code);
int var_slot(void);

#endif
