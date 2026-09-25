# Pac-Man

Four MazeMan mazes cycle per level, and the maze fades in from black.
Four ghosts leave the house on a timer. They hunt with the arcade's own
targets. They alternate scatter and chase. A power pill turns them blue and
they score 200, 400, 800 and 1600 when eaten. Contact costs a life. A 21 row
table makes each level faster. Seven sounds, and a siren that climbs as the
maze empties. The score, the lives and the level print in the strip above
the maze through the printf overlay. They print only when one of them
changes. Any key starts a game from the attract screen, where the ghosts
introduce themselves and chase Pac-Man. Steer with the arrows. The last
life leads to a game over screen, and a key there goes back to the attract
screen. Nothing halts. The ghosts' skirts ripple as they move.

The sprites are PNG strips beside the source, placed with `.sprite`, so
the IDE's sprite editor opens them. Pac-Man is twelve frames. Each ghost
is eight, two per facing. The frightened ghost is four and the eyes are
eight. Every frame is 14 by 14.

gensprites.py draws the strips from shapes: Pac-Man's disc, mouth and
eye, the ghosts' domes, wavy skirts and round eyes. Every edge is
anti-aliased. An edge pixel against the maze takes the body colour,
darkened by how much of it the shape covers. An edge inside the sprite
mixes its two colours. Running the script replaces hand edits to the
PNGs.

Hunting ghosts cover Pac-Man, and Pac-Man covers frightened ones. The GPU
draws higher sprite numbers on top. So Pac-Man is sprite 1, under the
ghosts, and again sprite 6, above them. Sprite 6 follows sprite 1. It
hides while a hunting ghost overlaps him, as the GPU's hit test says.

The maze is anti-aliased too. The dots and power pills have a dim rim,
and the wall curves have dim pixels on their inside. Straight walls stay
crisp. Palette entry 5 is the dim wall shade and 6 the dim dot shade. The
fades write them with the maze colours, so the sprites never use 1 to 6.

The mazes and sounds sit in .data as db lines. genmazes.mjs and
genpacsound.mjs of the browser project generated them.
