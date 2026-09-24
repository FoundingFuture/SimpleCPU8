// The friendly libraries: what a program written by a beginner includes.
//
// The generated headers (gpu.h, apu.h and the rest) are the machine's own
// vocabulary: one macro per command, one argument per port byte. These sit
// on top of them with the words a graphics or sound library usually has,
// plain int coordinates, and no port in sight. Each header has a library
// unit behind it, compiled in when the header is included and dropped
// function by function when nothing reaches it, so an unused library costs
// no program memory.
//
// Names are short and common on purpose. A program that wants one of them
// for itself leaves the header out.

#include "cc/libs.h"

namespace sc8::cc {

namespace {

// ---- graphics.h --------------------------------------------------------

const char* const GRAPHICS_H = R"(/* graphics.h. Drawing, text, images and sprites without a port in sight.
 *
 * Coordinates are ints, x to the right and y down, 0 to 255 on the screen
 * and anything off it clipped. A colour is a byte in the 3-3-2 palette:
 * rgb(r, g, b) with r and g from 0 to 7 and b from 0 to 3, or one of the
 * names below. setcolor picks the pen for every shape that follows.
 *
 *   #include <graphics.h>
 *   int main(void) {
 *       cls();
 *       setcolor(YELLOW);
 *       fillcircle(128, 128, 40);
 *       setcolor(WHITE);
 *       ellipse(128, 128, 100, 50);
 *       at(2, 30); print("HELLO");
 *       while (1) nextframe();
 *   }
 */
#ifndef __GRAPHICS_H__
#define __GRAPHICS_H__

#include <gpu.h>
#include <sys.h>
#include <rom.h>

#define rgb(r, g, b)  gpu_rgb(r, g, b)
#define BLACK    0
#define WHITE    255
#define RED      rgb(7, 0, 0)
#define GREEN    rgb(0, 7, 0)
#define BLUE     rgb(0, 0, 3)
#define YELLOW   rgb(7, 7, 0)
#define CYAN     rgb(0, 7, 3)
#define MAGENTA  rgb(7, 0, 3)
#define ORANGE   rgb(7, 4, 0)
#define GREY     rgb(4, 4, 2)
#define DARKGREY rgb(2, 2, 1)

/* The pen. Every shape below draws in it. */
void setcolor(unsigned char c);

/* The whole screen to black, or to one colour. */
void cls(void);
void clear(unsigned char c);

/* One pixel in the pen colour, and one read back. */
void plot(int x, int y);
unsigned char pixel(int x, int y);

/* Lines. moveto and lineto keep a pen position, line takes both ends. */
void moveto(int x, int y);
void lineto(int x, int y);
void line(int x1, int y1, int x2, int y2);

/* Shapes. The plain name is the outline, the fill name is solid. A
 * rectangle is its top left corner and its size. A circle and an ellipse
 * are their centre and their radius, or their two radii.
 */
void rect(int x, int y, int w, int h);
void fillrect(int x, int y, int w, int h);
void circle(int x, int y, int r);
void fillcircle(int x, int y, int r);
void ellipse(int x, int y, int rx, int ry);
void fillellipse(int x, int y, int rx, int ry);

/* Wait for the next frame, 60 a second, which is how a program paces
 * itself. frames() counts them, wrapping at 256.
 */
#define nextframe()   wait_frame()
#define frames()      gpu_frame()

/* A number from 0 up to but not including n. Any n up to 65535. */
unsigned int random(unsigned int n);

/* Text over the picture: 42 columns of 32 rows. at() puts the cursor,
 * print() writes a string, printf() formats like C: %d %u %hhu %x %s.
 * The text keeps its own colours and sits above every shape and sprite.
 */
#define at(col, row)          gpu_text_at((col), (row))
#define print(s)              gpu_printf(s)
#define printf                gpu_printf
#define textcolor(fg, bg)     gpu_text_style((fg), (bg), 0)
#define cleartext()           gpu_text_clear()

/* Images and sprites come from the cartridge. Declare one in ROM and name
 * it by its ROM_ constant:
 *
 *   __ROM const unsigned char pic[] = __image("pic.png");
 *   __ROM const unsigned char ship[] = __sprite("ship.png", 4);
 *   image(10, 20, ROM_pic);
 *   sprite(1, ROM_ship, 0);
 *   spriteat(1, 100, 100);
 *   show(1);
 *
 * There are 256 sprites, each up to 64 by 64, and higher numbers draw in
 * front. Pixel 0 is transparent. A group is a byte of eight bits that
 * collisions can be asked about. stamp bakes a sprite into the picture.
 */
#define image(x, y, r)            (moveto((x), (y)), gpu_blit(ROM_BANK(r), ROM_HI(r), ROM_LO(r)))
#define sprite(n, r, group)       gpu_sprite_def((n), ROM_BANK(r), ROM_HI(r), ROM_LO(r), (group))
void spriteat(unsigned char n, int x, int y);
void stamp(unsigned char n, int x, int y);
#define show(n)                   gpu_sprite_show(n)
#define hide(n)                   gpu_sprite_hide(n)
#define spriteframe(n, f)         gpu_sprite_frame((n), (f))
#define spriteflip(n, h, v)       gpu_sprite_flip((n), ((h) ? 1 : 0) | ((v) ? 2 : 0))

/* Collisions: bounding boxes, hidden sprites never hit. hit() answers
 * whether two sprites touch. hitgroups() answers the groups a sprite
 * touches. hitin() names the first sprite of a group touching n, from
 * number from, or 255 for none.
 */
#define hit(a, b)                 gpu_hit_test((a), (b))
#define hitgroups(n)              gpu_sprite_hits(n)
#define hitin(n, group, from)     gpu_hit_in_group((n), (group), (from))

/* A palette from the cartridge, then the default one again. */
#define palette(r)                gpu_load_palette(ROM_BANK(r), ROM_HI(r), ROM_LO(r))
#define resetpalette()            gpu_reset_palette()

#endif
)";

const char* const GRAPHICS_C = R"(#include <graphics.h>

static unsigned char __pen;

void setcolor(unsigned char c) {
    __pen = c;
    gpu_set_color(c);
}

void cls(void) { gpu_clear(0); }
void clear(unsigned char c) { gpu_clear(c); }

void plot(int x, int y) {
    gpu_plot((unsigned int)x >> 8, x & 255, (unsigned int)y >> 8, y & 255, __pen);
}

unsigned char pixel(int x, int y) {
    return gpu_read_pixel((unsigned int)x >> 8, x & 255, (unsigned int)y >> 8, y & 255);
}

void moveto(int x, int y) {
    gpu_move_to((unsigned int)x >> 8, x & 255, (unsigned int)y >> 8, y & 255);
}

void lineto(int x, int y) {
    gpu_line_to((unsigned int)x >> 8, x & 255, (unsigned int)y >> 8, y & 255);
}

void line(int x1, int y1, int x2, int y2) {
    moveto(x1, y1);
    lineto(x2, y2);
}

void fillrect(int x, int y, int w, int h) {
    int x2;
    int y2;
    if (w <= 0 || h <= 0) return;
    x2 = x + w - 1;
    y2 = y + h - 1;
    moveto(x, y);
    gpu_rect((unsigned int)x2 >> 8, x2 & 255, (unsigned int)y2 >> 8, y2 & 255);
}

void rect(int x, int y, int w, int h) {
    int x2;
    int y2;
    if (w <= 0 || h <= 0) return;
    x2 = x + w - 1;
    y2 = y + h - 1;
    moveto(x, y);
    lineto(x2, y);
    lineto(x2, y2);
    lineto(x, y2);
    lineto(x, y);
}

void fillcircle(int x, int y, int r) {
    moveto(x, y);
    gpu_circle(r);
}

void circle(int x, int y, int r) {
    moveto(x, y);
    gpu_ring(r);
}

/* The GPU draws an ellipse from a second radius on GPU_RADIUS_Y. The
 * command wrapper takes one radius, so the second port is written here.
 */
void fillellipse(int x, int y, int rx, int ry) {
    moveto(x, y);
    out(GPU_RADIUS, rx);
    out(GPU_RADIUS_Y, ry);
    out(GPU_CMD, CMD_CIRCLE);
}

void ellipse(int x, int y, int rx, int ry) {
    moveto(x, y);
    out(GPU_RADIUS, rx);
    out(GPU_RADIUS_Y, ry);
    out(GPU_CMD, CMD_RING);
}

/* Two bytes from the generator make a 16 bit number, then a remainder. */
unsigned int random(unsigned int n) {
    unsigned int v;
    if (n == 0) return 0;
    v = rand();
    v = (v << 8) | rand();
    return v % n;
}

void spriteat(unsigned char n, int x, int y) {
    gpu_sprite_move(n, (unsigned int)x >> 8, x & 255, (unsigned int)y >> 8, y & 255);
}

void stamp(unsigned char n, int x, int y) {
    gpu_stamp(n, (unsigned int)x >> 8, x & 255, (unsigned int)y >> 8, y & 255);
}
)";

// ---- sound.h -----------------------------------------------------------

const char* const SOUND_H = R"(/* sound.h. Notes, sound effects and tunes without a port in sight.
 *
 * The chip plays samples from the cartridge. A built in square wave means
 * a program can beep with no file at all. Notes are MIDI numbers: 60 is
 * middle C, 69 is A above it, and the names below spell them.
 *
 *   #include <sound.h>
 *   int main(void) {
 *       sound_init();
 *       beep(C5, 15);
 *       while (1) nextframe();
 *   }
 *
 * For a sample of your own, put a WAV beside the source:
 *
 *   __ROM const unsigned char boom[] = __sample("boom.wav");
 *   sample(1, ROM_boom, ROM_boom_SIZE, C4);   once, at power on
 *   play(1, C4);                              the sound effect
 *
 * A tune is a MIDI file: tune(ROM_song) starts it, tune_stop() ends it,
 * tune_playing() says whether it still runs.
 */
#ifndef __SOUND_H__
#define __SOUND_H__

#include <apu.h>
#include <sys.h>
#include <rom.h>

#define C3 48
#define D3 50
#define E3 52
#define F3 53
#define G3 55
#define A3 57
#define B3 59
#define C4 60
#define D4 62
#define E4 64
#define F4 65
#define G4 67
#define A4 69
#define B4 71
#define C5 72
#define D5 74
#define E5 76
#define F5 77
#define G5 79
#define A5 81
#define B5 83
#define C6 84

/* Load the square wave into slot 0 and give every track that voice. */
void sound_init(void);

/* A note on the beep track for a number of frames, then silence. The
 * program waits inside, so a beep is a pause as well.
 */
void beep(unsigned char note, unsigned char frames);

/* Start and stop a note on a track, 0 to 7, for a tune played by hand.
 * A track sounds up to eight notes at once.
 */
void noteon(unsigned char track, unsigned char note);
void noteoff(unsigned char track, unsigned char note);
void silence(void);

/* A sample from the cartridge into a slot, 0 to 63, at the note it was
 * recorded at. play sounds it on the effects track at a note, so a higher
 * note plays it faster. loop makes a slot repeat until stopped.
 */
#define sample(slot, r, size, root) \
    __defsample((slot), ROM_BANK(r), ROM_HI(r), ROM_LO(r), (size), (root))
void __defsample(unsigned char slot, unsigned char bank, unsigned char hi, unsigned char lo, unsigned int size, unsigned char root);
void play(unsigned char slot, unsigned char note);
#define loop(slot, on)   apu_loop_slot((slot), (on))

/* A MIDI file from the cartridge. */
#define tune(r)          (apu_load_midi(ROM_BANK(r), ROM_HI(r), ROM_LO(r)), apu_play())
#define tune_stop()      apu_stop()
#define tune_playing()   (apu_status() & 1)

#endif
)";

const char* const SOUND_C = R"(#include <sound.h>
#include "ROM.h"

/* One cycle of a square wave, 32 samples at 8000 a second: 250 Hz, which
 * is B below middle C. Looped, the chip shifts it to every other note.
 */
__ROM const unsigned char beepwave[32] = {
    224, 224, 224, 224, 224, 224, 224, 224, 224, 224, 224, 224, 224, 224, 224, 224,
    32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32
};
#define __SQUARE_ROOT 59
#define __BEEP_TRACK 7
#define __FX_TRACK 6
#define __SQUARE_SLOT 0

void __defsample(unsigned char slot, unsigned char bank, unsigned char hi, unsigned char lo, unsigned int size, unsigned char root) {
    apu_def_sample(slot, bank, hi, lo, size >> 8, size & 255, root);
}

void sound_init(void) {
    unsigned char t;
    apu_def_sample(__SQUARE_SLOT, ROM_beepwave_BANK, ROM_beepwave_HI, ROM_beepwave_LO, 0, 32, __SQUARE_ROOT);
    apu_loop_slot(__SQUARE_SLOT, 1);
    t = 0;
    while (t < 8) {
        apu_set_instrument(t, __SQUARE_SLOT, 0);
        t = t + 1;
    }
}

void noteon(unsigned char track, unsigned char note) {
    apu_note_on(track, note, 100);
}

void noteoff(unsigned char track, unsigned char note) {
    apu_note_off(track, note);
}

void silence(void) {
    apu_stop_all();
}

void beep(unsigned char note, unsigned char frames) {
    apu_note_on(__BEEP_TRACK, note, 100);
    while (frames) {
        wait_frame();
        frames = frames - 1;
    }
    apu_note_off(__BEEP_TRACK, note);
}

void play(unsigned char slot, unsigned char note) {
    apu_trigger(__FX_TRACK, slot, note, 100);
}
)";

// ---- keys.h ------------------------------------------------------------

const char* const KEYS_H = R"(/* keys.h. The keyboard and the controller.
 *
 * The controller is a level: up() is true for as long as the key is held.
 * The keyboard is a queue: key() gives the next key pressed, or 0 when
 * none is waiting, and never blocks. waitkey() blocks until one arrives.
 *
 *   #include <keys.h>
 *   if (left()) x = x - 1;
 *   if (fire()) shoot();
 *   c = key();
 *   if (c == 'Q') return 0;
 *
 * The arrow keys are the controller: up, down, left and right. Space is
 * space(), Enter is enter(), fire() is Ctrl or a pad button. Codes from
 * key() are ASCII, letters in upper case, Enter 13, Escape 27, space 32.
 */
#ifndef __KEYS_H__
#define __KEYS_H__

#include <io.h>
#include <sys.h>

#define up()      io_pressed(BTN_UP)
#define down()    io_pressed(BTN_DOWN)
#define left()    io_pressed(BTN_LEFT)
#define right()   io_pressed(BTN_RIGHT)
#define fire()    io_pressed(BTN_FIRE)
#define space()   io_pressed(BTN_SPACE)
#define enter()   io_pressed(BTN_ENTER)
#define shift()   io_held(MOD_SHIFT)
#define ctrl()    io_held(MOD_CTRL)

#define KEY_ENTER  13
#define KEY_ESCAPE 27
#define KEY_SPACE  32

/* The next key pressed, 0 for none. Releases are skipped. */
unsigned char key(void);

/* Wait for a key, frame by frame, and give it. */
unsigned char waitkey(void);

/* Throw away every key waiting, so an old press cannot start something. */
void flushkeys(void);

#endif
)";

const char* const KEYS_C = R"(#include <keys.h>

unsigned char key(void) {
    unsigned char k;
    while (1) {
        k = io_key();
        if (k == 0) return 0;
        if (!io_key_is_up(k)) return io_key_code(k);
    }
}

unsigned char waitkey(void) {
    unsigned char k;
    while (1) {
        k = key();
        if (k) return k;
        wait_frame();
    }
}

void flushkeys(void) {
    while (io_key()) { }
}
)";

// ---- math.h ------------------------------------------------------------

const char* const MATH_H = R"(/* math.h. The usual functions on double, run by the coprocessor.
 *
 * A double is eight bytes in RAM and every operation is one coprocessor
 * command, so these cost about what an add costs. Angles are radians.
 *
 *   #include <math.h>
 *   double a = 3.14159 / 4;
 *   int x = 128 + (int)(cos(a) * 100);
 *   int y = 128 + (int)(sin(a) * 100);
 */
#ifndef __MATH_H__
#define __MATH_H__

#include <acp.h>

#define PI 3.14159265358979

double sqrt(double x);
double sin(double x);
double cos(double x);
double tan(double x);
double asin(double x);
double acos(double x);
double atan(double x);
double atan2(double y, double x);
double pow(double x, double y);
double exp(double x);
double log(double x);
double log10(double x);
double fabs(double x);
double hypot(double x, double y);

/* Whole numbers. abs and the larger and smaller of two. */
int abs(int x);
int max(int a, int b);
int min(int a, int b);

/* An angle in degrees, 0 to 359, to radians. */
double radians(int degrees);

#endif
)";

// Every function points the coprocessor at a block of one, two or three
// doubles. The block is the library's own, so a call leaves the compiler's
// scratch alone and acp_run resends the address every time.
const char* const MATH_C = R"(#include <math.h>

static double __m[3];

static double __unary(unsigned char cmd, double x) {
    __m[0] = x;
    acp_point(__m, ACP_F64, 1, 1);
    acp_run(cmd);
    return __m[1];
}

static double __binary(unsigned char cmd, double a, double b) {
    __m[0] = a;
    __m[1] = b;
    acp_point(__m, ACP_F64, 1, 1);
    acp_run(cmd);
    return __m[2];
}

double sqrt(double x) { return __unary(ACP_SQRT, x); }
double sin(double x) { return __unary(ACP_SIN, x); }
double cos(double x) { return __unary(ACP_COS, x); }
double tan(double x) { return __unary(ACP_TAN, x); }
double asin(double x) { return __unary(ACP_ASIN, x); }
double acos(double x) { return __unary(ACP_ACOS, x); }
double atan(double x) { return __unary(ACP_ATAN, x); }
double exp(double x) { return __unary(ACP_EXP, x); }
double log(double x) { return __unary(ACP_LOG, x); }
double log10(double x) { return __unary(ACP_LOG10, x); }
double fabs(double x) { return __unary(ACP_ABS, x); }
double atan2(double y, double x) { return __binary(ACP_ATAN2, y, x); }
double pow(double x, double y) { return __binary(ACP_POW, x, y); }
double hypot(double x, double y) { return __binary(ACP_HYPOT, x, y); }

int abs(int x) { return x < 0 ? -x : x; }
int max(int a, int b) { return a > b ? a : b; }
int min(int a, int b) { return a < b ? a : b; }

double radians(int degrees) { return degrees * (PI / 180.0); }
)";

// ---- disk.h ------------------------------------------------------------

const char* const DISK_H = R"(/* disk.h. Named files in the cartridge, the storage BASIC saves to.
 *
 * A file is text under a name of up to 16 letters and digits. There are
 * 64 of them. save writes a string, load reads one back into a buffer of
 * the size given, and both answer 1 when it worked.
 *
 *   #include <disk.h>
 *   char buf[200];
 *   if (load("SCORES", buf, 200)) at(0, 0), printf("%s", buf);
 *   save("SCORES", "1000 EDDIE");
 */
#ifndef __DISK_H__
#define __DISK_H__

#include <storage.h>

unsigned char save(char *name, char *text);
unsigned char load(char *name, char *buf, unsigned int room);
unsigned char erase(char *name);
/* The names, one per line, into a buffer. */
unsigned char catalog(char *buf, unsigned int room);
#define files()   sto_count()

#endif
)";

const char* const DISK_C = R"(#include <disk.h>

unsigned char save(char *name, char *text) {
    sto_save(text, name);
    return sto_status() == STO_OK;
}

unsigned char load(char *name, char *buf, unsigned int room) {
    sto_load(buf, name, room);
    return sto_status() == STO_OK;
}

unsigned char erase(char *name) {
    sto_delete(name);
    return sto_status() == STO_OK;
}

unsigned char catalog(char *buf, unsigned int room) {
    sto_catalog(buf, room);
    return sto_status() == STO_OK;
}
)";

}  // namespace

const std::vector<Library>& libraries() {
  static const std::vector<Library> L = {
      {"graphics.h", "<graphics.c>", GRAPHICS_H, GRAPHICS_C},
      {"sound.h", "<sound.c>", SOUND_H, SOUND_C},
      {"keys.h", "<keys.c>", KEYS_H, KEYS_C},
      {"math.h", "<math.c>", MATH_H, MATH_C},
      {"disk.h", "<disk.c>", DISK_H, DISK_C},
  };
  return L;
}

}  // namespace sc8::cc
