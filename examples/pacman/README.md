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
screen. Nothing halts. The mazes,
sprites and sounds sit in .data as db lines. genmazes.mjs, gensprites.mjs
and genpacsound.mjs of the browser project generated them.
