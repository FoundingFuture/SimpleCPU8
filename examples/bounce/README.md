# Bounce

A ball, a bat and a score, written in C with the friendly libraries. The
project has the larger layout: sources in src/, pictures in assets/ and
the ROM in build/.

Move the bat with the arrow keys and keep the ball in play. Every bounce
off the bat scores a point and the ball speeds up a little. Three misses
end the game, and any key starts another.

    simplecpu-make examples/bounce
    simplecpu --rom examples/bounce/build/bounce.rom

Read src/main.c from the top. graphics.h draws, sound.h beeps, keys.h
reads the arrows, and the two PNG files become sprites through __sprite.
