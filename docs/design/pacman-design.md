# Pac-Man design

A maze game for SimpleCPU-8. One Pac-Man and four ghosts, with the arcade's
real targeting rules. Dots, power pills, and a set of mazes cycled per level.

Built so far: the maze, the dots, movement, and every tunnel pair a maze
declares. Levels cycle. Four ghosts hunt with the arcade's targets, turn blue
on a power pill, and go home as eyes when eaten. Lives and the sound are in.
What is still owed is the score display and the HUD strip. This is the design
record for the whole game.

## Contents

- [What it is and is not](#what-it-is-and-is-not)
- [The screen](#the-screen)
- [Maze data](#maze-data)
- [The mazes that ship](#the-mazes-that-ship)
- [Tunnels](#tunnels)
- [Drawing the maze](#drawing-the-maze)
- [Two tables buy what the CPU lacks](#two-tables-buy-what-the-cpu-lacks)
- [Movement](#movement)
- [Ghosts](#ghosts)
- [The house](#the-house)
- [Difficulty per level](#difficulty-per-level)
- [Scoring and the strip](#scoring-and-the-strip)
- [Game states](#game-states)
- [Sound](#sound)
- [Memory budget](#memory-budget)
- [Testing](#testing)
- [Still open](#still-open)

## What it is and is not

The reference is `/Volumes/ext/Users/eddie/prog/c++/MazeMan`, a 15,000 line
C++ version, and the arcade ROM behaviour it follows. This takes the parts
that make the game and leaves the rest.

In: one maze at a time from a set, with its own dots and power pills. Four
ghosts
with distinct chase targets, scatter and chase alternation, and frightened
mode. Eaten ghosts return home. The tunnel, lives, and a score.

Out: fruit, cutscenes, and the house dot-counters that decide release order.
Those are bookkeeping rather than ideas.

The per-level tables stay, in a reduced form. A game that plays the same at
level ten as at level one is not a game.

## The screen

Graphics mode. The maze is 28 by 31 tiles of 8 pixels, so it occupies x 0 to
223 and y 0 to 247. That leaves two free regions.

```text
  x=0                      224   255
  +----------------------+-----+
  |                      | 1UP |   4 characters wide,
  |        maze          | 0250|   31 rows tall
  |      28 x 31         |     |
  |                      | LIFE|
  +----------------------+-----+
  |                            |   32 characters, one row, unused
  +----------------------------+
```

Walls and dots are drawn into video memory once when a level starts, and
never redrawn. Eating a dot paints an 8 by 8 square of background over it.

Five sprites carry all the motion: Pac-Man is sprite 1 and the ghosts are 2
to 5. Spare lives are sprites 6 to 8 in the side strip. That is cheaper than
drawing them, and it reads better than a number.

The printf overlay draws the strip. It rides over graphics and sprites, so
the score costs no video memory and no sprite.

Text mode cannot be used for the maze. `composeFrame` returns before the
sprite loop in that mode, so sprites would not appear over it.

### State and art

A ghost carries two facts that must never disagree. The state byte says what it
is, and the sprite strip says what a player sees. A player who sees eyes reads
them as harmless.

The draw owns the art. `adstrip` runs from `actdraw`, works the strip out from
the state byte, and applies it only when the answer changes. Nothing else
writes a strip at runtime.

It used to be written at five sites beside the state. A sixth wrote states and
no art at all. `placeghosts` put an eaten ghost back as a normal one and left
it wearing the eyes. It was lethal, and drawn as the one thing on the board
that cannot touch you. The owner met it in a real game.

The price is one frame. `unfright` runs from `modestep` and `cteat` from
`contact`, both of which the frame loop calls after the walk that draws, so a
state changed there is worn on the next frame. That is 16 milliseconds against
a disagreement that used to last the rest of the game.

`astrip` caches what each sprite wears, one byte an actor. It ships as 0, which
is no strip at all. The first frame after a load or a Restart therefore always
defines. It is a cache and not a second source of truth. `adstrip` recomputes
the answer every frame, and this only says whether it has been applied.

An eaten ghost returns to normal at home and nowhere else. The life-loss reset
counts as home, because it puts every ghost back where the level starts it.

### Who owns which palette entry

The maze owns entries 1, 2 and 3, and rewrites all three on every fade. A
sprite pixel in that range is not the colour it was drawn in. It is whatever
the maze last put there.

The ghosts' pupils were index 3. They came out as the power pill's pale peach,
which is invisible against the white of the eye. They are index 4 now. The
program sets that entry to RGB (24, 24, 24) at startup, because the GPU's own
default for index 4 is a dark green. Index 0 is transparent, so true black
cannot be a sprite pixel at all.

Entry 4 is written at the top of the program and nowhere else. `fput`,
`fisnap` and `foblack` each write exactly three entries from index 1, so
nothing in either fade reaches it. Restart is the one thing that does. It
powers the GPU on with the default palette back, and it re-runs the program
from that line.

Every sprite strip is held out of 1 to 3 by a test rather than by care.

## Maze data

Each maze is 31 rows of 28 characters, the format MazeMan uses, held in the
cartridge as a `.data` blob.

```asm
.data
maze1:  db "############################"
        db "#............##............#"
        db "#o####.#####.##.#####.####o#"
```

The character set is MazeMan's, so a maze copies across unchanged.

| character | meaning |
|---|---|
| `#` | wall |
| `.` | dot |
| `o` | power pill |
| space | empty |
| `-` | ghost house door, two adjacent cells |
| `H` | the house point, where an eaten ghost returns |
| `P` | Pac-Man's start, a two cell span |
| `1` to `9` | tunnel endpoints, each digit used exactly twice |

A maze therefore says everything about itself. Nothing outside it holds a
coordinate, and a new maze can put the house and the tunnels anywhere.

The door is a wall to Pac-Man and open to ghosts. That one rule keeps him out
of the house with no special case anywhere else.

`P` spans two cells because Pac-Man starts between them rather than on one.
The rightmost `P` is kept and he starts on its left edge.

Ghost starts come from the door rather than from markers. The `H` cell says
which side of the door is inside, because the house is what holds it. Outside
is the other side. Blinky begins on the cell outside the door. The other three
begin on `H` and the cells beside it, and leave through the door.

Read that side from the maze, never assume it. Three of the four shipped mazes
put the house below its door. Metro Station puts it above, door on row 22 and
`H` on row 20. Code that reaches for the cell above the door finds house
interior on that maze.

A level copies its maze into a working array in RAM with one `CMD_COPY`, the
command built for this:

```asm
        OUT GPU_CART_BANK, get_bankbyte(maze1)
        OUT GPU_CART_HI, get_highbyte(maze1)
        OUT GPU_CART_LO, get_lowbyte(maze1)
        OUT GPU_DEST_HI, get_highbyte(work)
        OUT GPU_DEST_LO, get_lowbyte(work)
        OUT GPU_LEN_HI, 3        ; 868 bytes
        OUT GPU_LEN_LO, $64
        OUT GPU_CMD, CMD_COPY
```

The working array is the one the game reads and writes. Eating a dot writes a
space over the `.`. So the layout and the dot state are one array rather than
two. Clearing every dot advances to the next maze, cycling at the end.

One pass over the 868 tiles then does four jobs at once. It draws the walls
and dots into video memory and records where `P` and `G` were. It writes a
space over those markers, so they do not affect movement. It also counts the
dots and pills.

That count is why no maze needs a header. Every maze holds a different number
of dots. A level ends when the counter reaches zero, not when it matches a
total written down somewhere else. The counter is 16 bits, so a maze can hold
more than 255 dots without anyone thinking about it.

Power pills count. Clearing a maze means eating both.

The copy is why the mazes live in ROM. The CPU cannot read the cartridge.
Without `CMD_COPY` each maze would have to sit in the assembled RAM image.

## The mazes that ship

The first four from MazeMan, unchanged.

| level | name | tunnel pairs | dots | pills |
|---|---|---|---|---|
| 1 | CLASSIC | 1 | 240 | 4 |
| 2 | Parking Lot | 2 | 270 | 6 |
| 3 | Metro Station | 2 | 218 | 4 |
| 4 | Photo Opportunity | 2 | 258 | 4 |

Level five returns to CLASSIC and the cycle repeats, while the level table
keeps making the game harder.

Four different dot counts and two different pill counts. That is why the draw
pass counts them and nothing compares against a total.

A script converts MazeMan's C++ header into `.data` blobs, the way
`scripts/genaudio.mjs` builds the audio demo. Hand-copying 124 rows is how a
maze picks up a typo that only shows on level three.

## Tunnels

A tunnel is a pair of cells sharing a digit. Entering one puts the actor on
the other, and turns it to face the way out of that mouth.

This is more than wrapping at the screen edge, and the shipped mazes need it
to be. Metro Station's second pair sits at columns 1 and 26, so nothing ever
leaves the maze. Photo Opportunity's links row 4 to row 27, which is a warp
between two unrelated places.

So the design reads the pairs out of the layout into a small table, at most
nine of them. An actor reaching an endpoint is moved to its partner. Two
pairs is all the shipped mazes use.

The move happens once. The actor then leaves along the partner's own open
side, which takes it off that centre. Without a step it would sit on the
paired endpoint and be sent straight back.

Leaving along the partner's corridor, rather than carrying the old direction
through, is what Metro Station forces. Both mouths of its second pair are
vertical pockets, walled left, right and below. An actor enters heading down,
so carrying that direction through would centre it inside the wall at row 22.

### The slow zone

A ghost crawls through a tunnel. The speed is the level table's seventh byte
and the zone is 40 pixels, five tiles either side of a mouth.

A zone, not the mouth tile itself. A mouth is one position, and an actor
stands on it for a single tick. A slowdown hung off the warp would be one
pixel long, and invisible.

The zone runs along the corridor and nowhere else. Nothing in the tunnel table
says which way a mouth's corridor runs. The direction table beside it picks
the axis to widen, and the other axis has to match the mouth exactly.

Widening both would make the zone a square five tiles either way. At CLASSIC's
row 14 that reaches the long side corridors, and slows a ghost nowhere near a
tunnel.

Two actors are left alone. Eyes go home at full speed, because the trip home
is not a difficulty. Pac-Man keeps his own speed through a tunnel, which is
what makes a tunnel worth running to.

## Drawing the maze

Painting 868 tiles takes roughly 4000 instructions, which is a visible sweep
at the teaching speeds. The palette removes the problem without making the
drawing any faster.

The maze uses palette entries of its own, one for walls, one for dots, one
for pills. At the start of a level those three are set to black. The draw
then runs against a black background, painting black on black, and nothing
appears however long it takes.

When the draw finishes, the three entries fade from black up to their real
colours over 30 frames. The maze rises out of the background, and it looks
deliberate rather than slow.

A cleared level fades the same three entries back down to black over 30
frames. The next maze is drawn into the dark and faded up. One level runs
into the next with nothing to see between them.

Every level works this way, the first as much as the tenth. A draw that is
only hidden sometimes is a draw someone will eventually watch happen.

Each fade step writes three palette entries, so twelve `OUT`s a frame for 30
frames. Nothing is redrawn. The pixels were always there.

This is why the three entries must belong to the maze alone. Sprites and the
text overlay use other indices and stay visible throughout. The Ready message
can sit on screen while the maze arrives behind it.

## Two tables buy what the CPU lacks

The machine has no multiply and no shift. Two tables supply what the game
needs, and this is what the demo teaches.

A tile index is `row times 28 plus col`, which the CPU cannot compute
directly. A **row table** of 31 sixteen-bit entries holds the address of each
row's first byte. An index becomes one lookup and an add.

Ghost targeting compares squared distances. A **squares table** of 64
sixteen-bit entries turns `dx squared plus dy squared` into two lookups and
one 16-bit add. Sixty-four entries because a doubled target vector reaches
about 60 tiles. A distance is clamped to 63 before the lookup.

Together that is 62 and 128 bytes. Both are built in `.data` and copied into
RAM, or written as `dw` lists in `.ram`.

This is the same move the circle demo makes with its quarter-wave sine table.
It is the point a student should take away. When the hardware will not do the
arithmetic, precompute it.

## Movement

Every actor holds a pixel x, a pixel y, a direction, and a speed accumulator.
Pac-Man also holds a buffered turn.

Speed is fractional without any division. Each frame the actor adds its speed
byte to its accumulator. It moves one pixel when that carries.

| actor | speed byte | fraction |
|---|---|---|
| Pac-Man | 204 | 80% |
| ghost, normal | 191 | 75% |
| ghost, frightened | 128 | 50% |
| ghost, eaten | 255 | full |

Two things change what a tick adds, and neither writes an actor's own speed
byte. The record keeps the speed the level gave it.

A ghost inside a tunnel corridor adds the level table's tunnel speed instead,
40 percent at level 1 and 50 percent from level 5. Eyes on their way home are
not slowed, and neither is Pac-Man. See the tunnel section.

Pac-Man stops outright for a frame when he eats a dot. A power pill costs him
three. Those are the arcade's own figures. They are why a player taking an
empty line through a maze outruns the dots he is eating. The pause is counted
in whole frames, so he loses the tick rather than a fraction of a pixel.

An actor is centred on a tile when both coordinates are multiples of 8, which
is `AND A <- 7` twice. Everything expensive happens only there.

- Pac-Man adopts his buffered turn if the maze allows it.
- A ghost picks its next direction.
- A dot or pill under Pac-Man is eaten.

Per frame the work is five position updates. The buffered turn is what makes
the game feel right. Press up before a junction and he turns on arrival,
rather than dropping the input.

A tunnel mouth is a tile, and an actor whose centre lands on one is moved to
its partner. No code looks for an edge. That one rule covers a border pair, an
interior pair, and a pair joining two different rows. The layout decides where
the tunnels are, and how many.

## Ghosts

A hunting ghost chooses like this. At a tile centre, take the three directions
that are not a reversal and drop any facing a wall. Of what is left, take the
one whose tile is nearest the target. All four hunt through that one routine
and only the target differs. A frightened ghost and a pair of eyes each choose
another way, both below.

| ghost | chase target | scatter corner |
|---|---|---|
| Blinky | Pac-Man's tile | top right |
| Pinky | 4 tiles ahead of Pac-Man | top left |
| Inky | Blinky reflected through the point 2 ahead of Pac-Man | bottom right |
| Clyde | Pac-Man, or his corner when closer than 8 tiles | bottom left |

Inky's target is `2P minus B`, where P is two tiles ahead of Pac-Man and B is
Blinky's tile. That needs signed bytes and no multiply beyond a doubling,
which is an add.

A global timer alternates scatter and chase. A mode change reverses every
ghost except a pair of eyes. That is the one exception to the no-reversal
rule, and the thing that makes them look alive. Eyes are left out because a
reversal is a hunt's business and they are not hunting.

A power pill sets every ghost frightened. They slow down, turn blue, and
choose randomly at each tile centre. Eating them scores 200, then 400, 800
and 1600 within one pill.

An eaten ghost becomes eyes, moves at full speed, and heads for the house
door. It re-enters and rejoins as normal.

Eyes steer by a distance field. Once per maze the game floods the grid breadth
first out of the tile inside the door. That stores, for every tile, how many
steps it is from home. At a centre the eyes read their four neighbours and
step to the smallest, with no reversal rule. Downhill on a distance field
cannot cycle, so the eyes arrive from any tile of any maze.

Aiming at the door and choosing greedily was the earlier rule. On Photo
Opportunity it closed a ten tile cycle in the bottom pocket. 300 of the 312
tiles got home, and eyes eaten in the other twelve never did.

Four states per ghost: in the house, normal, frightened, eaten. MazeMan uses
seven, splitting house entry and exit into their own states.

## The house

Ghosts start in the house and leave one at a time on a timer. Blinky starts
outside. The others leave after a fixed delay each, rather than the arcade's
dot counters.

Leaving is a scripted move to the door and then up. Entering is the same in
reverse. Neither uses the targeting routine, because the house is the one
place the maze rules do not apply.

### Leaving the house during a fright

Two ghosts stand at the door in the same state 0, and they leave it
differently.

A ghost that was WAITING in the house never stopped being a hunting ghost. It
walks out blue and at the frightened speed. On level 1 the window is 360
frames and Pinky's release is 64. A pill eaten early would otherwise put one
red ghost on a board of blue ones.

A ghost that was EATEN and has walked home as eyes has been reset by getting
there. It leaves normal, at the ghost speed and in its own colours. It can
catch him on the frame it appears, with the window still open. That is the
arcade's rule.

`revive` is the byte that tells them apart, one per actor beside `houstk`.
`ehin` sets it when a pair of eyes reaches the house and `ashfree` spends it
on the way out. `placeghosts` clears all five. A mark left standing over a
level change or a death would send the next ghost out normal on a board of
blue ones.

## Difficulty per level

Seven bytes a level, held in the cartridge and copied into RAM at startup
with one `CMD_COPY`. Twenty-one rows is 147 bytes. A level past the last row
uses the last row, which is what the arcade does.

| byte | meaning |
|---|---|
| 0 | Pac-Man's speed |
| 1 | ghost speed |
| 2 | frightened speed |
| 3 | fright duration, in device frames |
| 4 | dots remaining when Blinky speeds up |
| 5 | scatter duration, in device frames |
| 6 | ghost speed inside a tunnel |

Nothing indexes this table. A pointer starts at row zero and advances seven
bytes when the level does. The machine never has to multiply by seven.

A speed byte is the fraction of a pixel per frame, times 256. So 205 is 80
percent and 255 is the fastest this model goes, one pixel every frame.

A duration is in units of 8 frames, which is a sixth of a second at 60 frames
a second. The unit is 8 because a frame counter ANDed with 7 is free, and
dividing by anything else is not.

These are the arcade's own figures converted to those units.

| level | pac | ghost | fright speed | fright | elroy | scatter | tunnel |
|---|---|---|---|---|---|---|---|
| 1 | 205 | 192 | 128 | 45 | 20 | 52 | 102 |
| 2 | 230 | 218 | 141 | 38 | 30 | 52 | 115 |
| 3 | 230 | 218 | 141 | 30 | 40 | 52 | 115 |
| 4 | 230 | 218 | 141 | 22 | 40 | 52 | 115 |
| 5 | 255 | 243 | 154 | 15 | 40 | 38 | 128 |
| 6 | 255 | 243 | 154 | 38 | 50 | 38 | 128 |
| 7 | 255 | 243 | 154 | 15 | 50 | 38 | 128 |
| 8 | 255 | 243 | 154 | 15 | 50 | 38 | 128 |
| 9 | 255 | 243 | 154 | 8 | 60 | 38 | 128 |
| 10 | 255 | 243 | 154 | 38 | 60 | 38 | 128 |
| 11 | 255 | 243 | 154 | 15 | 60 | 38 | 128 |
| 12 | 255 | 243 | 154 | 8 | 80 | 38 | 128 |
| 13 | 255 | 243 | 154 | 8 | 80 | 38 | 128 |
| 14 | 255 | 243 | 154 | 22 | 80 | 38 | 128 |
| 15 | 255 | 243 | 154 | 8 | 100 | 38 | 128 |
| 16 | 255 | 243 | 154 | 8 | 100 | 38 | 128 |
| 17 | 255 | 243 | 154 | 0 | 100 | 38 | 128 |
| 18 | 255 | 243 | 154 | 8 | 100 | 38 | 128 |
| 19 | 255 | 243 | 154 | 0 | 120 | 38 | 128 |
| 20 | 255 | 243 | 154 | 0 | 120 | 38 | 128 |
| 21 | 230 | 243 | 154 | 0 | 120 | 38 | 128 |

Read the columns and the ramp is visible.

Speed rises on both sides. Ghosts go from 192 to 243, which is 75 percent to
95 percent. Pac-Man goes from 205 to 255 and then drops back to 230 at level
21. He is quicker than them until that last row, where they are quicker than
him for good.

Fright collapses. Six seconds at level 1, two by level 5, one by level 9, and
nothing from level 17. Levels 6, 10 and 14 are the arcade's own reprieves,
and they are kept. A pill still reverses every ghost when the duration is
zero. It no longer turns them blue or edible.

Elroy arrives sooner. Blinky speeds up when the dots left fall below byte 4,
which the arcade calls Cruise Elroy. That threshold climbs from 20 to 120, so
by the late levels he is dangerous from almost the first dot. It reads the
dot counter the draw pass already keeps, so it costs one comparison.

Scatter shortens from 52 units to 38. The ghosts spend more of each level
hunting and less of it walking to their corners.

Level 22 and beyond use row 21, which is what the arcade does.

The table is read by level, never by maze. Level 6 uses row 6 whichever of
the four mazes is on screen.

That has one visible consequence, accepted rather than overlooked. An Elroy
threshold of 20 dots is a later moment in Parking Lot's 270 than in CLASSIC's
240. The bigger mazes give a little more peace before Blinky speeds up.

The alternative is a threshold per maze per level. That is four times the
table for a difference nobody would name.

## Scoring and the strip

The strip is four characters wide, so it holds `1UP` and `LIFE` but not
`SCORE`. Every score in the game is a multiple of ten. So the display shows
the score divided by ten, with an implied trailing zero. Four digits then
reach 99,990, past any realistic game.

Dots score 10 and pills 50. Ghosts score 200, 400, 800 and 1600 in sequence.

The bottom row is 32 characters and stays free, in case the score should
move there later.

## Game states

Ready, playing, dying, level cleared, and game over. Dying is a short pause,
not an animation.

Ready and level cleared are the fades. Ready holds for the 30 frames the
maze takes to arrive, and level cleared for the 30 it takes to leave.

The level ends when the dot counter hits zero. Nothing compares against a
fixed total, because each maze has its own.

Pac-Man starts with three lives. A death resets the actors to their starting
positions and leaves the dots as they were.

Nothing leaves game over, so reaching it ends the program. The frame loop's
game over arm halts the CPU on the next arrival. The board, the score and the
maze stay on screen, because the GPU holds them. Restart starts a new game.

A loop polling the frame counter and acting on nothing would run the machine
flat out for that same picture. Anything that later gives game over work to
do has to move the halt, not work around it.

## Sound

Seven samples on the audio chip, synthesised by `scripts/genpacsound.mjs` and
carried in the cartridge. Two are the chomp, three are the held bed, and two
are stings.

| sound | slot | kind | fires when |
|---|---|---|---|
| chomp, upper tone | 0 | one shot | a dot is eaten |
| chomp, lower tone | 1 | one shot | the next dot is eaten |
| siren | 2 | looped | the ghosts are hunting |
| fright warble | 3 | looped | any ghost is blue |
| eyes | 4 | looped | any ghost is going home |
| ghost caught | 5 | one shot | a blue ghost is eaten |
| death | 6 | one shot | a ghost catches him |

The bed is one looping voice on a track of its own. `sndbed` runs once a
frame and works out which of the three the board asks for. It touches the chip
only when that answer changes. A looping voice retriggered every frame
restarts sixty times a second, which is a buzz rather than a siren.

Eyes outrank the fright warble, and the fright warble outranks the siren. A
ghost travelling home stays audible while the others are still blue.

The siren is a PURE SINE, and that is not a matter of taste. It is the one
sample the game pitches. Resampling a sample upward carries every partial up
with it. A partial that fits under 4 kHz at the root can be over it at the top of the
climb. What is over Nyquist folds back down. It returns as energy unrelated to
the note being played.

The siren was a five partial sawtooth. 5 x 631 x 1.587 is 5008 Hz, which came
back as 2992 Hz of hiss on the higher steps. Measured through the chip, energy
above 3 kHz went from 0.66 percent at note 60 to 4.49 at note 68. With one
partial it is 0.007 and 0.55. The generator now refuses a siren whose top
partial would clear Nyquist at the top of the climb.

What is left at 0.55 percent is the chip's own resampler, which truncates
rather than interpolates. Rooting the sample at the top of the ladder and
playing it down was measured and is worse, at 1.49 percent. Playing down
repeats samples.

The siren climbs in five steps as the maze empties. One sample plays at five
notes two semitones apart, so a higher step is both higher and faster. The
four thresholds are fifths of the level's own dot count. They are worked out
once per draw by repeated subtraction. The shipped mazes hold 244, 276, 222
and 262 dots, so a table of absolute counts would climb at a different point
on each.

Three moments take the bed away. Any game state but playing, which covers the
dying pause and the game over screen. A level change, so the fade between two
mazes is quiet. The program's first line, which a Reset re-runs while the chip
is still looping.

The bed plays at velocity 12 against the chomp's 110 and a sting's 127. The
owner asked for a third of the loudness the siren had at 100. Loudness roughly
doubles per 10 dB, so a third of it is 15.85 dB down. That is an amplitude
factor of 0.161 and not 0.33. Measured through the chip at the host's gain,
the siren's rendered RMS goes from 12.98 to 2.11, which is 15.76 dB. Velocity
is the knob rather than the sample. The sample is 8 bit too, and scaling it
would spend its own resolution as well as the output's.

A looped sample has to close on itself. Its phase is pulled to a whole number
of cycles. The loop then starts at the phase where the waveform moves least
across one sample. A step at the join is a click on every pass, which at a 0.4
second loop is 150 clicks a minute.

## Memory budget

| what | bytes |
|---|---|
| working maze | 868 |
| squares table | 128 |
| row table | 62 |
| level table | 126 |
| tunnel table | 36 |
| five actors | 40 |
| game state | 32 |
| sound | 13 |
| revive marks | 5 |
| strip cache | 6 |

About 1316 bytes of the 65536 available. The scalars are declared first so
they sit in the zero page, where `addr8` addressing reaches them. The three
tables are read through a D register, so they can sit anywhere above.

Each maze costs 868 bytes of cartridge, and the cartridge holds a megabyte.

## Testing

Everything is reachable through a headless `Session`, the way the invaders
tests work. Poke the working maze to stage a board, run, and read RAM back.

The targeting rules are the tests worth having, because they fail in ways
nobody sees.

- Pinky aims four tiles ahead, clamped into the grid when that runs off it.
- Clyde flips to his corner at exactly eight tiles.
- Inky's target is on the far side of the point ahead of Pac-Man.
- A mode change reverses every ghost once, and only once.
- A ghost never reverses on its own.
- Eating the fourth ghost on one pill scores 1600.
- A tunnel moves an actor to its pair, and does not send it straight back.
- An interior tunnel works, away from the maze border.
- A tunnel between two different rows works.
- The held bed changes only when the board changes, never once a frame.
- Every looped sample joins its own first byte without a step.
- The siren carries no folded energy at any step of its climb.
- No sprite pixel uses a palette entry the maze rewrites.
- A ghost revived from eyes leaves the house normal, one waiting leaves blue.
- Eyes eat no dot on the way home.
- Every ghost is drawn as the state it is in, in every state.
- An eaten ghost is never left normal in the eyes strip.
- A maze with a different dot count still ends at zero, not at 240.
- Level twenty-two uses the last row of the table rather than reading past it.
- Blinky speeds up when the dot count crosses his threshold, and not before.

## Still open

Nothing. Every question this design raised has an answer above, and the
values are real rather than placeholders. What is left is building it.
