# Sprite groups and group collision

## Contents

- [Why](#why)
- [Groups](#groups)
- [The four commands](#the-four-commands)
- [Ports](#ports)
- [What a collision means](#what-a-collision-means)
- [The frame cache](#the-frame-cache)
- [What this does not do](#what-this-does-not-do)
- [Implementation plan](#implementation-plan)

## Why

A game asks about kinds of thing, not about sprite numbers. Does enemy fire
hit the player. Did a player shot reach an invader. The GPU answers a
different question. `CMD_COLLIDE_ALL` gives every sprite the id of its first
overlapping partner. The program is left to work out what that partner was.

Today it works that out from the id. This is the shipped invaders demo:

```asm
LD D1 <- hits+100
LD A <- [D1]
SUB A <- 97          ; invader indices are 1..96
JC youlose           ; partner below 97: an invader reached you
```

Group membership encoded as an id range, decoded with range compares on a
machine with no CMP. Every game would reinvent it.

There is a second limit. That table holds the FIRST partner only. A player
touched by a bullet and an enemy reports one of them. Which one is an
accident of sprite order.

## Groups

Eight groups. One byte, one bit each. The controller packs seven buttons
into a byte for the same reason. One AND is what the CPU can test.

A sprite holds a group MASK, not a group number. So one sprite can be an
enemy and a solid at once. Zero means the sprite is in no group at all.

The mask is set by `CMD_SPRITE_DEF`, on `GPU_SPRITE_GROUP`. There is no
command to change it afterwards. Redefining the sprite is how a sprite
changes groups, and that is the owner's decision.

A redefine re-reads the blob and resets the animation frame to 0. A program
that changes a group mid-animation has to put the frame back. The reference
says so on the row.

An ungrouped sprite never appears in a group answer. It still collides, and
`CMD_HIT_TEST` and `CMD_HIT_SCAN` see it exactly as before.

## The four commands

All four answer on `GPU_HIT`, except the last, which writes RAM. A result
survives the clear that wipes the arguments.

| command | in | out |
|---|---|---|
| `CMD_SPRITE_HITS` | `GPU_SPRITE` | groups touching that sprite |
| `CMD_GROUP_HITS` | `GPU_GROUP` | groups touching that group |
| `CMD_HIT_IN_GROUP` | `GPU_SPRITE`, `GPU_GROUP_B`, `GPU_HIT_FROM` | first sprite in the group |
| `CMD_COLLIDE_GROUP_ALL` | none | 256 masks, at `MAP_GROUPS` |

`CMD_GROUP_HITS` reports a group against ITSELF. Two invaders overlapping
sets the invader bit. Suppressing it would be a rule the reader cannot see,
and masking your own bit off is one AND.

`CMD_HIT_IN_GROUP` searches from `GPU_HIT_FROM` upward and answers 0 when
nothing is left. So a program walks every overlapping sprite by feeding the
last answer plus one back in. Without that a blast radius damages one enemy
and the rest survive.

`CMD_COLLIDE_GROUP_ALL` is the bulk form, and it earns its place the way
`CMD_COLLIDE_ALL` does. Ten sprites a frame costs ten `CMD_SPRITE_HITS`
commands. It costs one command and ten loads here.

## Ports

Argument names follow `GPU_SPRITE_B`, which is already the name for the
second sprite of a pair.

| alias | port | used by |
|---|---|---|
| `GPU_GROUP` | `$02` | `CMD_GROUP_HITS`, where the group is the first argument |
| `GPU_GROUP_B` | `$03` | `CMD_HIT_IN_GROUP`, where a sprite came first |
| `GPU_HIT_FROM` | `$04` | `CMD_HIT_IN_GROUP`, the id to search from |
| `GPU_SPRITE_GROUP` | `$06` | `CMD_SPRITE_DEF`, the membership mask |

Commands take a new block at `$40`, because the collision family at `$28` has
no room beside it.

`MAP_GROUPS` joins `GPU_MAPS` as the address `CMD_COLLIDE_GROUP_ALL` writes.

## What a collision means

The group commands inherit the existing test exactly, and add nothing to it.

- Axis-aligned bounding boxes. A diagonal shot clips the empty corner of a
  wide sprite.
- A hidden sprite collides with nothing.
- A sprite never collides with itself.

## The frame cache

Answering a group question means testing every visible pair, which is 256 by
256. Three group commands a frame would do that three times.

So the answers are computed once and kept until something moves. The key is
the GPU's own `version` counter, which every mutation already bumps.

The counter is bumped conditionally in four places, all guarded by
`s.visible`. Moving a hidden sprite does not bump it. That is exactly right
here. A hidden sprite collides with nothing, so a move that skips the bump
cannot change an answer. `CMD_SPRITE_SHOW` bumps it, and that is the moment
the sprite starts to matter.

The cache holds one mask per sprite and one mask per group. Both fall out of
the same pass.

## What this does not do

Stated so a reader does not go looking.

- No overlap rectangle and no depth. You learn that two sprites touch, never
  where or by how much. Pushing out of a wall is the CPU's problem.
- No pixel-perfect test. See above.
- No count. How many sprites are touching this one needs the walk that
  `CMD_HIT_IN_GROUP` provides.
- No group readback. A program knows what it set.

## Implementation plan

Each step is a failing test first, then the code, then green.

1. `Sprite` holds a `group`. `CMD_SPRITE_DEF` reads `GPU_SPRITE_GROUP` and
   stores it. Omitting the port gives 0, so every existing program is
   unchanged.
2. The cache: one mask per sprite, one per group, rebuilt when `version`
   moves. A test proves it is rebuilt after a move and after a show.
3. `CMD_SPRITE_HITS`.
4. `CMD_GROUP_HITS`, including a group against itself.
5. `CMD_HIT_IN_GROUP`, including the walk from `GPU_HIT_FROM` and the 0 that
   ends it.
6. `CMD_COLLIDE_GROUP_ALL` and `MAP_GROUPS`.
7. Documentation: the alias table, the command rows, the result column, and
   a groups section in the manual. The tables above go into
   `docs/gpu-ports.md`. The coverage tests refuse anything missed.
8. Invaders drops the id-range compare for a group query, as the worked
   example. Golden bytes re-recorded.
