/* Bounce: a ball, a bat and a score, on the friendly libraries. */
#include <graphics.h>
#include <sound.h>
#include <keys.h>

__ROM const unsigned char ballpic[] = __sprite("ball.png", 2);
__ROM const unsigned char batpic[] = __sprite("bat.png");

#define BALL 1
#define BAT 2
#define BAT_Y 240

int bx, by;      /* the ball's top left corner */
int dx, dy;      /* how far it moves each frame */
int batx;
unsigned int score;
unsigned char misses;
unsigned char flash;

void serve(void)
{
    bx = 120;
    by = 40;
    dx = random(2) ? 2 : -2;
    dy = 2;
    flash = 0;
}

void newgame(void)
{
    score = 0;
    misses = 0;
    batx = 112;
    serve();
}

void hud(void)
{
    at(1, 0);
    printf("SCORE %u   MISSES %hhu", score, misses);
}

void background(void)
{
    int i;
    clear(BLACK);
    setcolor(DARKGREY);
    for (i = 8; i < 256; i = i + 24) line(0, i, 255, i);
    setcolor(BLUE);
    rect(0, 8, 256, 248);
}

/* One frame of play. Answers 1 while the ball is in play. */
unsigned char step(void)
{
    if (left() && batx > 0) batx = batx - 3;
    if (right() && batx < 224) batx = batx + 3;

    bx = bx + dx;
    by = by + dy;
    if (bx <= 0 || bx >= 240) { dx = -dx; beep(C4, 1); }
    if (by <= 8) { dy = -dy; beep(C4, 1); }

    /* The bat: the ball is falling, its bottom reaches the bat's top and
     * they overlap sideways. Where it hits the bat sets the angle.
     */
    if (dy > 0 && by + 16 >= BAT_Y && by + 16 <= BAT_Y + 4 && bx + 16 > batx && bx < batx + 32) {
        dy = -dy;
        if (bx + 8 < batx + 10) dx = -3;
        else if (bx + 8 > batx + 22) dx = 3;
        score = score + 1;
        if (score % 5 == 0 && dy > -6) dy = dy - 1;
        flash = 6;
        beep(G5, 2);
        hud();
    }
    if (by > 250) {
        misses = misses + 1;
        beep(C3, 10);
        hud();
        if (misses == 3) return 0;
        serve();
    }

    spriteframe(BALL, flash ? 1 : 0);
    if (flash) flash = flash - 1;
    spriteat(BALL, bx, by);
    spriteat(BAT, batx, BAT_Y);
    return 1;
}

void gameover(void)
{
    hide(BALL);
    at(16, 14);
    print("GAME OVER");
    at(13, 16);
    print("PRESS ANY KEY");
    flushkeys();
    waitkey();
    at(13, 16);
    print("             ");
    at(16, 14);
    print("         ");
}

int main(void)
{
    sound_init();
    sprite(BALL, ROM_ballpic, 0);
    sprite(BAT, ROM_batpic, 0);
    textcolor(WHITE, BLACK);
    while (1) {
        background();
        newgame();
        hud();
        show(BALL);
        show(BAT);
        while (step()) nextframe();
        gameover();
    }
}
