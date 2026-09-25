; SPACE INVADERS, all in assembly. The invaders are animated sprites sharing
; one strip. They march as a block, drop and reverse at the walls, and rain
; bombs. Move the cannon with the arrow keys or A and D, fire with Z, Ctrl or
; Space. Clear the board and the next level arrives one row wider, eight rows
; of eight invaders becoming twelve, after which the count holds. The game ends when the lowest invader
; still alive reaches the cannon's row, or the cannon loses all three lives. Collisions come
; from the GPU, by GROUP. Every sprite is defined into one of four groups and
; the game asks about kinds of thing rather than about sprite numbers.
; Sprite 0 is left unused so a table entry of zero always means no hit.
; Run at 1k instr/s or faster. Double-click Frame to watch it slowly.

; the ground line at the bottom
        OUT GPU_COLOR, $92
        OUT GPU_CMD, CMD_SET_COLOR
; The collision table lives in RAM. Point the GPU at it once.
        OUT GPU_CMD, CMD_MEMMAP

        OUT GPU_X, 0
        OUT GPU_Y, 250
        OUT GPU_CMD, CMD_MOVE_TO
        OUT GPU_X, 255
        OUT GPU_Y, 252
        OUT GPU_CMD, CMD_RECT

; --- every invader slot, sprites 1..96, all from one strip. A level shows
; the first ninv of them and hides the rest, so growing a level costs no
; new sprite work. The GPU has 256 slots and this uses 100 of them, so the
; ceiling below is the screen's and not the hardware's.
; DESIGN: every argument is written inside the loop. The data ports clear
; after each command, so the address written once before it survived only
; because invstrip is the first thing in .data and clears to its own address
; of zero. Anything placed ahead of it would have defined 95 invaders from
; the wrong blob. The group byte has no such luck: it would clear to 0 and
; leave every invader after the first in no group at all.
        LD A <- 1
        LD [ii] <- A
defi:   LD A <- [ii]
        OUTA GPU_SPRITE
        OUT GPU_SRC_BANK, get_bankbyte(invstrip)
        OUT GPU_SRC_HI, get_highbyte(invstrip)
        OUT GPU_SRC_LO, get_lowbyte(invstrip)
        OUT GPU_SPRITE_GROUP, 1        ; group 0: the invaders
        OUT GPU_CMD, CMD_SPRITE_DEF
        LD A <- [ii]
        INC A
        LD [ii] <- A
        SUB A <- 97
        JZ defcan
        JMP defi

; --- cannon sprite 19
defcan: OUT GPU_SPRITE, 100
        OUT GPU_SRC_BANK, get_bankbyte(canspr)
        OUT GPU_SRC_HI, get_highbyte(canspr)
        OUT GPU_SRC_LO, get_lowbyte(canspr)
        OUT GPU_SPRITE_GROUP, 2        ; group 1: the cannon
        OUT GPU_CMD, CMD_SPRITE_DEF
        OUT GPU_SPRITE, 100
        OUT GPU_CMD, CMD_SPRITE_SHOW

; --- player bullet sprite 20, hidden
        OUT GPU_SPRITE, 101
        OUT GPU_SRC_BANK, get_bankbyte(pbspr)
        OUT GPU_SRC_HI, get_highbyte(pbspr)
        OUT GPU_SRC_LO, get_lowbyte(pbspr)
        OUT GPU_SPRITE_GROUP, 4        ; group 2: the player's shot
        OUT GPU_CMD, CMD_SPRITE_DEF

; --- two invader bullets sprites 21 and 22
        OUT GPU_SPRITE, 102
        OUT GPU_SRC_BANK, get_bankbyte(ibspr)
        OUT GPU_SRC_HI, get_highbyte(ibspr)
        OUT GPU_SRC_LO, get_lowbyte(ibspr)
        OUT GPU_SPRITE_GROUP, 8        ; group 3: the invaders' bombs
        OUT GPU_CMD, CMD_SPRITE_DEF
        OUT GPU_SPRITE, 103
        OUT GPU_SRC_BANK, get_bankbyte(ibspr)
        OUT GPU_SRC_HI, get_highbyte(ibspr)
        OUT GPU_SRC_LO, get_lowbyte(ibspr)
        OUT GPU_SPRITE_GROUP, 8        ; group 3
        OUT GPU_CMD, CMD_SPRITE_DEF

; --- audio: define the two sound effects, slot 0 fire, slot 1 hit
        OUT APU_CART_BANK, get_bankbyte(fxfire)
        OUT APU_CART_HI, get_highbyte(fxfire)
        OUT APU_CART_LO, get_lowbyte(fxfire)
        OUT APU_ARG, 3          ; 1000 bytes, high byte first
        OUT APU_ARG2, 232
        OUT APU_NOTE, 60
        OUT APU_SLOT, 0
        OUT APU_CMD, CMD_DEF_SAMPLE
        OUT APU_CART_BANK, get_bankbyte(fxhit)
        OUT APU_CART_HI, get_highbyte(fxhit)
        OUT APU_CART_LO, get_lowbyte(fxhit)
        OUT APU_ARG, 7          ; 1800 bytes, high byte first
        OUT APU_ARG2, 8
        OUT APU_NOTE, 60
        OUT APU_SLOT, 1
        OUT APU_CMD, CMD_DEF_SAMPLE
        OUT APU_CART_BANK, get_bankbyte(fxboom)
        OUT APU_CART_HI, get_highbyte(fxboom)
        OUT APU_CART_LO, get_lowbyte(fxboom)
        OUT APU_ARG, 10         ; 2600 bytes, high byte first. get_sizelo
        OUT APU_ARG2, $28       ; cannot see a db block, so it is written out
        OUT APU_NOTE, 60
        OUT APU_SLOT, 2
        OUT APU_CMD, CMD_DEF_SAMPLE

        JSR setboard
        JSR repos
        JSR drawlives

; =========================== main loop ===========================
main:   IN GPU_FRAME
        SUB A <- [fr]
        JZ main
        IN GPU_FRAME
        LD [fr] <- A

        IN A <- IO_CONTROLLER
        LD [inp] <- A
        AND A <- BTN_LEFT
        JZ nol
        LD A <- [canx]
        SUB A <- 3
        JC nol
        LD [canx] <- A
nol:    LD A <- [inp]
        AND A <- BTN_RIGHT
        JZ nor
        LD A <- [canx]
        ADD A <- 3
        LD [t0] <- A
        SUB A <- 249
        JC dor
        JMP nor
dor:    LD A <- [t0]
        LD [canx] <- A
nor:    OUT GPU_SPRITE, 100
        LD A <- [canx]
        OUTA GPU_SPRITE_X
        OUT GPU_SPRITE_Y, 240
        OUT GPU_CMD, CMD_SPRITE_MOVE

; fire on a fresh press if the bullet is idle
        LD A <- [inp]
        AND A <- $70
        LD [t0] <- A
        JZ nofire
        LD A <- [pf]
        JZ dofire
        JMP nofire
dofire: LD A <- [pba]
        JZ spawnb
        JMP nofire
spawnb: LD A <- 1
        LD [pba] <- A
        LD A <- [canx]
        ADD A <- 3
        LD [pbx] <- A
        LD A <- 232
        LD [pby] <- A
        OUT GPU_SPRITE, 101
        OUT GPU_CMD, CMD_SPRITE_SHOW
        OUT APU_TRACK, 0        ; the fire sound effect
        OUT APU_SLOT, 0
        OUT APU_NOTE, 60
        OUT APU_ARG, 110
        OUT APU_CMD, CMD_TRIGGER
nofire: LD A <- [t0]
        LD [pf] <- A

; player bullet rises
        LD A <- [pba]
        JZ noplayer
        LD A <- [pby]
        SUB A <- 6
        LD [pby] <- A
        SUB A <- 10
        JC pbgone
        OUT GPU_SPRITE, 101
        LD A <- [pbx]
        OUTA GPU_SPRITE_X
        LD A <- [pby]
        OUTA GPU_SPRITE_Y
        OUT GPU_CMD, CMD_SPRITE_MOVE
        JMP noplayer
pbgone: LD A <- 0
        LD [pba] <- A
        OUT GPU_SPRITE, 101
        OUT GPU_CMD, CMD_SPRITE_HIDE
noplayer:

; move the block every 6 frames
        LD A <- [movet]
        INC A
        LD [movet] <- A
        SUB A <- 6
        JC nostep
        LD A <- 0
        LD [movet] <- A
        LD A <- [anim]
        XOR A <- 1
        LD [anim] <- A
        JSR edges            ; which columns are still in play
        LD A <- [bdir]
        JZ goright
        LD A <- [bx]
        SUB A <- 4
        LD [bx] <- A
        ADD A <- [mincx]     ; the leftmost invader still alive
        SUB A <- 8
        JC edge
        JMP stepped
goright: LD A <- [bx]
        ADD A <- 4
        LD [bx] <- A
        ADD A <- [maxcx]     ; the rightmost invader still alive
        SUB A <- 248         ; march to the right wall so bombs reach a far-right cannon
        JC stepped
edge:   LD A <- [bdir]
        XOR A <- 1
        LD [bdir] <- A
        LD A <- [by]
        ADD A <- 8
        LD [by] <- A
stepped: JSR repos
nostep:

; the floor: the lowest invader still alive reaching the cannon's row ends it.
; Three rows put that at by 208, exactly where the line was before.
        LD A <- [by]
        ADD A <- [maxcy]
        SUB A <- 240
        JC notfloor
        JMP youlose
notfloor:

        JSR ibfire
        JSR ibmove

; --- collisions: one command fills a table in RAM, then a pointer reads it
;
; A table is 256 bytes, so it goes to memory rather than a byte at a time
; through a port. Reading an entry is one load through a pointer where it
; used to be a port write and a port read.
; player bullet: did it hit an invader? One command names the invader, where
; this used to read a table entry and decide what the partner was by testing
; its id against 97. Sprite numbers meant something, and now groups do.
        LD A <- [pba]
        JZ nopcol
        OUT GPU_SPRITE, 101
        OUT GPU_GROUP_B, 1             ; the invader group
        OUT GPU_CMD, CMD_HIT_IN_GROUP
        IN GPU_HIT -> A
        AND A <- $FF        ; IN sets no flags, so mask before the branch
        LD [t0] <- A
        JZ nopcol
pbkill: LD A <- [t0]         ; the invader sprite hit
        OUTA GPU_SPRITE
        OUT GPU_CMD, CMD_SPRITE_HIDE
        LD D1 <- alive       ; alive[hit-1] := 0
        LD A <- [t0]
        SUB A <- 1
        JZ akill
        LD [t1] <- A
awalk:  INC D1
        LD A <- [t1]
        SUB A <- 1
        LD [t1] <- A
        JZ akill
        JMP awalk
akill:  LD A <- 0
        LD [D1] <- A
        OUT APU_TRACK, 2        ; the explosion, on a track of its own
        OUT APU_SLOT, 2
        OUT APU_NOTE, 60
        OUT APU_ARG, 118
        OUT APU_CMD, CMD_TRIGGER
        LD A <- 0            ; retire the bullet
        LD [pba] <- A
        OUT GPU_SPRITE, 101
        OUT GPU_CMD, CMD_SPRITE_HIDE
        LD A <- [aliven]
        SUB A <- 1
        LD [aliven] <- A
        JZ nextlvl
nopcol:
; cannon: an invader on it ends the game, a bomb costs a life. One command
; gives BOTH answers as bits, which the old table could not: it held one
; partner per sprite, so a cannon touched by a bomb and an invader on the
; same frame heard about whichever came first in sprite order.
        OUT GPU_SPRITE, 100
        OUT GPU_CMD, CMD_SPRITE_HITS
        IN GPU_HIT -> A
        LD [t0] <- A         ; save it: the AND below destroys A
        AND A <- 1           ; group 0, an invader reached you
        JZ canbomb
        JMP youlose
canbomb: LD A <- [t0]
        AND A <- 8           ; group 3, a bomb
        JZ nocancol
; which bomb. The group said what kind, this says which one.
        OUT GPU_SPRITE, 100
        OUT GPU_GROUP_B, 8
        OUT GPU_CMD, CMD_HIT_IN_GROUP
        IN GPU_HIT -> A
        SUB A <- 102
        JZ hitib0
        LD A <- 0
        LD [ib1a] <- A
        OUT GPU_SPRITE, 103
        OUT GPU_CMD, CMD_SPRITE_HIDE
        JMP dohurt
hitib0: LD A <- 0
        LD [ib0a] <- A
        OUT GPU_SPRITE, 102
        OUT GPU_CMD, CMD_SPRITE_HIDE
dohurt: JSR hurt
nocancol:
        JMP main

; ======================= subroutines =======================
; the live edges: the leftmost and rightmost columns that still hold an
; invader. The block's own width cannot decide the turn, or a lone survivor
; would bounce off walls its dead neighbours used to reach. bx is a signed
; offset, so bx plus a column offset is the invader's screen x either way.
edges:  LD A <- $FF
        LD [mincx] <- A
        LD A <- 0
        LD [maxcx] <- A
        LD [maxcy] <- A
        LD [ei] <- A
eloop:  LD D1 <- alive
        LD A <- [ei]
        LD A <- [D1+A]
        JZ enext             ; dead, so it owns no edge
        LD D1 <- cxt
        LD A <- [ei]
        LD A <- [D1+A]
        LD [ecx] <- A
        SUB A <- [mincx]
        JC esetmin           ; further left than any seen yet
        JMP echkmax
esetmin: LD A <- [ecx]
        LD [mincx] <- A
echkmax: LD A <- [maxcx]
        SUB A <- [ecx]
        JC esetmax           ; further right than any seen yet
        JMP echkmy
esetmax: LD A <- [ecx]
        LD [maxcx] <- A
; every live invader is asked for its row, not only the ones that widened the
; block. Column offsets repeat each row, so the rightmost column is found on
; row 0 and never beaten, and a row test hung off that branch would never run.
echkmy: LD D1 <- cyt
        LD A <- [ei]
        LD A <- [D1+A]
        LD [ecy] <- A
        LD A <- [maxcy]
        SUB A <- [ecy]
        JC esetmy            ; lower than any row seen yet
        JMP enext
esetmy: LD A <- [ecy]
        LD [maxcy] <- A
enext:  LD A <- [ei]
        INC A
        LD [ei] <- A
        SUB A <- [ninv]
        JZ edgesx
        JMP eloop
edgesx: RET

repos:  LD A <- 0
        LD [ii] <- A
; The frame and the move are two commands, so the sprite is named twice.
; They share a data port: the frame sits where a move reads x's high byte,
; and one batch doing both would fling the invader off the screen.
rloop:  LD A <- [ii]
        ADD A <- 1           ; invader sprite = index + 1
        OUTA GPU_SPRITE
        LD A <- [anim]
        OUTA GPU_SPRITE_FRAME
        OUT GPU_CMD, CMD_SPRITE_FRAME
        LD A <- [ii]
        ADD A <- 1
        OUTA GPU_SPRITE
        LD D1 <- cxt
        LD A <- [ii]
        LD A <- [D1+A]
        LD [t0] <- A
        LD A <- [bx]
        ADD A <- [t0]
        OUTA GPU_SPRITE_X
        LD D1 <- cyt
        LD A <- [ii]
        LD A <- [D1+A]
        LD [t0] <- A
        LD A <- [by]
        ADD A <- [t0]
        OUTA GPU_SPRITE_Y
        OUT GPU_CMD, CMD_SPRITE_MOVE
        LD A <- [ii]
        INC A
        LD [ii] <- A
        SUB A <- [ninv]
        JZ reposx
        JMP rloop
reposx: RET

; a fire cooldown paces the bombs; then pick a live invader and drop one
ibfire: LD A <- [firet]
        JZ ibready
        SUB A <- 1
        LD [firet] <- A
        RET
ibready: LD A <- [rng]       ; advance the rng
        LD [t0] <- A
        ADD A <- [t0]
        ADD A <- [t0]
        ADD A <- [t0]
        ADD A <- [t0]
        ADD A <- 1
        LD [rng] <- A
ibmod:  SUB A <- [ninv]    ; reduce it into 0..ninv-1
        JC ibgot
        JMP ibmod
ibgot:  ADD A <- [ninv]
        LD [t1] <- A
        LD D1 <- alive
        LD A <- [t1]
        LD A <- [D1+A]
        JZ ibno              ; a dead invader: try again next frame
        LD A <- [ib0a]
        JZ ibuse0
        LD A <- [ib1a]
        JZ ibuse1
        RET                  ; both bombs in flight: wait
ibuse0: LD A <- 1
        LD [ib0a] <- A
        LD A <- [t1]
        JSR ibpos
        LD A <- [t0]
        LD [ib0x] <- A
        LD A <- [t2]
        LD [ib0y] <- A
        OUT GPU_SPRITE, 102
        OUT GPU_CMD, CMD_SPRITE_SHOW
        LD A <- 30
        LD [firet] <- A
        RET
ibuse1: LD A <- 1
        LD [ib1a] <- A
        LD A <- [t1]
        JSR ibpos
        LD A <- [t0]
        LD [ib1x] <- A
        LD A <- [t2]
        LD [ib1y] <- A
        OUT GPU_SPRITE, 103
        OUT GPU_CMD, CMD_SPRITE_SHOW
        LD A <- 30
        LD [firet] <- A
        RET
ibno:   RET

; screen position of the invader whose index is in A: t0 := x+3, t2 := y+10
ibpos:  LD [t1] <- A
        LD D1 <- cxt
        LD A <- [t1]
        LD A <- [D1+A]
        LD [t0] <- A
        LD A <- [bx]
        ADD A <- [t0]
        ADD A <- 3
        LD [t0] <- A
        LD D1 <- cyt
        LD A <- [t1]
        LD A <- [D1+A]
        LD [t2] <- A
        LD A <- [by]
        ADD A <- [t2]
        ADD A <- 10
        LD [t2] <- A
        RET

; move both invader bullets down, retire off the bottom
ibmove: LD A <- [ib0a]
        JZ ibm1
        LD A <- [ib0y]
        ADD A <- 3           ; a slower fall gives more frames over the cannon
        LD [ib0y] <- A
        SUB A <- 250
        JC ib0ok
        LD A <- 0
        LD [ib0a] <- A
        OUT GPU_SPRITE, 102
        OUT GPU_CMD, CMD_SPRITE_HIDE
        JMP ibm1
ib0ok:  OUT GPU_SPRITE, 102
        LD A <- [ib0x]
        OUTA GPU_SPRITE_X
        LD A <- [ib0y]
        OUTA GPU_SPRITE_Y
        OUT GPU_CMD, CMD_SPRITE_MOVE
ibm1:   LD A <- [ib1a]
        JZ ibmx
        LD A <- [ib1y]
        ADD A <- 3           ; a slower fall gives more frames over the cannon
        LD [ib1y] <- A
        SUB A <- 250
        JC ib1ok
        LD A <- 0
        LD [ib1a] <- A
        OUT GPU_SPRITE, 103
        OUT GPU_CMD, CMD_SPRITE_HIDE
        RET
ib1ok:  OUT GPU_SPRITE, 103
        LD A <- [ib1x]
        OUTA GPU_SPRITE_X
        LD A <- [ib1y]
        OUTA GPU_SPRITE_Y
        OUT GPU_CMD, CMD_SPRITE_MOVE
ibmx:   RET

; The screen goes white for one device frame, so a hit is seen and not only
; heard. The background is drawn once at startup and never again, so it is
; saved before the flash and pasted back after it. Sprites compose on top of
; video memory every frame, so they ride over the white rather than
; vanishing into it.
flash:  OUT GPU_CMD, CMD_SAVE_SCREEN
        OUT GPU_COLOR, $FF
        OUT GPU_CMD, CMD_CLEAR
; Hold it for one frame. The main loop's own wait would eat this one, so the
; wait is here rather than left to the next time round.
fwait:  IN GPU_FRAME
        SUB A <- [fr]
        JZ fwait
        IN GPU_FRAME
        LD [fr] <- A
        OUT GPU_X, 0
        OUT GPU_Y, 0
        OUT GPU_CMD, CMD_RESTORE_SCREEN
        RET

; lose a life, redraw the row, game over at zero
hurt:   JSR flash
        OUT APU_TRACK, 1        ; the hit sound effect
        OUT APU_SLOT, 1
        OUT APU_NOTE, 60
        OUT APU_ARG, 120
        OUT APU_CMD, CMD_TRIGGER
        LD A <- [lives]
        SUB A <- 1
        LD [lives] <- A
        JSR drawlives
        LD A <- [lives]
        JZ youlose
        RET

; draw the remaining lives as green squares along the top
drawlives: OUT GPU_COLOR, 0
        OUT GPU_CMD, CMD_SET_COLOR
        OUT GPU_X, 2
        OUT GPU_Y, 2
        OUT GPU_CMD, CMD_MOVE_TO
        OUT GPU_X, 70
        OUT GPU_Y, 10
        OUT GPU_CMD, CMD_RECT
        OUT GPU_COLOR, $1C
        OUT GPU_CMD, CMD_SET_COLOR
        LD A <- 0
        LD [t0] <- A
        LD A <- 4
        LD [t1] <- A
dllp:   LD A <- [t0]
        SUB A <- [lives]
        JC dlone
        RET
dlone:  LD A <- [t1]
        OUTA GPU_X
        OUT GPU_Y, 3
        OUT GPU_CMD, CMD_MOVE_TO
        LD A <- [t1]
        ADD A <- 6
        OUTA GPU_X
        OUT GPU_Y, 8
        OUT GPU_CMD, CMD_RECT
        LD A <- [t1]
        ADD A <- 10
        LD [t1] <- A
        LD A <- [t0]
        INC A
        LD [t0] <- A
        JMP dllp

; a cleared board starts the next level, one row of eight wider until the
; screen runs out of room. Twelve rows is the cap: the lowest row starts at
; y 192 and still has 48 pixels to fall before it reaches the cannon, which is
; six wall bounces. Fourteen rows would leave two, and fifteen would end the
; game before the block moved. Past the cap the count holds and the levels
; keep coming until the player dies.
nextlvl: LD A <- [level]
        INC A
        LD [level] <- A
        LD A <- [ninv]
        ADD A <- 8
        LD [ninv] <- A
        SUB A <- 97          ; past the last row that fits
        JC lvlfit
        LD A <- 96
        LD [ninv] <- A
lvlfit: JSR setboard
        JMP main

; put the block back at the top and decide who is on it: the first ninv
; invaders live and show, the rest stay hidden.
setboard: LD A <- 24         ; the block starts over, top left
        LD [bx] <- A
        LD A <- 16
        LD [by] <- A
        LD A <- 0
        LD [bdir] <- A
        LD [movet] <- A
        LD A <- [ninv]
        LD [aliven] <- A
        LD D1 <- alive       ; revive the ones this level uses, hide the rest
        LD A <- 0
        LD [ii] <- A
slloop: LD A <- [ii]
        SUB A <- [ninv]
        JC slon
        LD A <- 0
        JMP slput
slon:   LD A <- 1
slput:  LD [D1] <- A
        INC D1
        LD A <- [ii]
        ADD A <- 1
        OUTA GPU_SPRITE
        LD A <- [ii]
        SUB A <- [ninv]
        JC slshow
        OUT GPU_CMD, CMD_SPRITE_HIDE
        JMP slnext
slshow: OUT GPU_CMD, CMD_SPRITE_SHOW
slnext: LD A <- [ii]
        INC A
        LD [ii] <- A
        SUB A <- 96
        JZ slx
        JMP slloop
slx:    JSR edges
        RET

youlose: OUT GPU_COLOR, $E0
endflash: OUT GPU_CMD, CMD_CLEAR
        HLT

.ram
ii:     db 0
t0:     db 0
t1:     db 0
t2:     db 0
fr:     db 0
inp:    db 0
canx:   db 124
pba:    db 0
pbx:    db 0
pby:    db 0
pf:     db 0
bx:     db 24
by:     db 16
bdir:   db 0
ninv:   db 24
level:  db 1
maxcy:  db $20
ecy:    db 0
mincx:  db 0
maxcx:  db $64
ecx:    db 0
ei:     db 0
movet:  db 0
anim:   db 0
aliven: db 18
rng:    db 7
firet:  db 20
lives:  db 3
ib0a:   db 0
ib0x:   db 0
ib0y:   db 0
ib1a:   db 0
ib1x:   db 0
ib1y:   db 0
alive:  db $01, $01, $01, $01, $01, $01, $01, $01, $01, $01, $01, $01, $01, $01, $01, $01
        db $01, $01, $01, $01, $01, $01, $01, $01, $00, $00, $00, $00, $00, $00, $00, $00
        db $00, $00, $00, $00, $00, $00, $00, $00, $00, $00, $00, $00, $00, $00, $00, $00
        db $00, $00, $00, $00, $00, $00, $00, $00, $00, $00, $00, $00, $00, $00, $00, $00
        db $00, $00, $00, $00, $00, $00, $00, $00, $00, $00, $00, $00, $00, $00, $00, $00
        db $00, $00, $00, $00, $00, $00, $00, $00, $00, $00, $00, $00, $00, $00, $00, $00
cxt:    db $00, $14, $28, $3C, $50, $64, $78, $8C, $00, $14, $28, $3C, $50, $64, $78, $8C
        db $00, $14, $28, $3C, $50, $64, $78, $8C, $00, $14, $28, $3C, $50, $64, $78, $8C
        db $00, $14, $28, $3C, $50, $64, $78, $8C, $00, $14, $28, $3C, $50, $64, $78, $8C
        db $00, $14, $28, $3C, $50, $64, $78, $8C, $00, $14, $28, $3C, $50, $64, $78, $8C
        db $00, $14, $28, $3C, $50, $64, $78, $8C, $00, $14, $28, $3C, $50, $64, $78, $8C
        db $00, $14, $28, $3C, $50, $64, $78, $8C, $00, $14, $28, $3C, $50, $64, $78, $8C
cyt:    db $00, $00, $00, $00, $00, $00, $00, $00, $10, $10, $10, $10, $10, $10, $10, $10
        db $20, $20, $20, $20, $20, $20, $20, $20, $30, $30, $30, $30, $30, $30, $30, $30
        db $40, $40, $40, $40, $40, $40, $40, $40, $50, $50, $50, $50, $50, $50, $50, $50
        db $60, $60, $60, $60, $60, $60, $60, $60, $70, $70, $70, $70, $70, $70, $70, $70
        db $80, $80, $80, $80, $80, $80, $80, $80, $90, $90, $90, $90, $90, $90, $90, $90
        db $A0, $A0, $A0, $A0, $A0, $A0, $A0, $A0, $B0, $B0, $B0, $B0, $B0, $B0, $B0, $B0

.data
invstrip:db $02, $08, $08, $00, $00, $1C, $1C, $1C, $1C, $00, $00, $00, $1C, $1C, $1C, $1C, $1C
        db $1C, $00, $1C, $1C, $00, $1C, $1C, $00, $1C, $1C, $1C, $1C, $1C, $1C, $1C, $1C
        db $1C, $1C, $00, $00, $1C, $00, $00, $1C, $00, $00, $00, $1C, $00, $1C, $1C, $00
        db $1C, $00, $1C, $00, $00, $00, $00, $00, $00, $1C, $00, $00, $00, $00, $00, $00
        db $00, $00, $00, $00, $1C, $1C, $1C, $1C, $00, $00, $00, $1C, $1C, $1C, $1C, $1C
        db $1C, $00, $1C, $1C, $00, $1C, $1C, $00, $1C, $1C, $1C, $1C, $1C, $1C, $1C, $1C
        db $1C, $1C, $00, $1C, $00, $1C, $1C, $00, $1C, $00, $1C, $00, $1C, $00, $00, $1C
        db $00, $1C, $00, $1C, $00, $00, $00, $00, $1C, $00, $00, $00, $00, $00, $00, $00
        db $00, $00
canspr: db $01, $08, $08
        db $00, $00, $00, $E0, $E0, $00, $00, $00, $00, $00, $00, $E0, $E0, $00, $00, $00
        db $00, $00, $E0, $E0, $E0, $E0, $00, $00, $00, $E0, $E0, $E0, $E0, $E0, $E0, $00
        db $E0, $E0, $E0, $E0, $E0, $E0, $E0, $E0, $E0, $E0, $E0, $E0, $E0, $E0, $E0, $E0
        db $E0, $E0, $E0, $E0, $E0, $E0, $E0, $E0, $E0, $E0, $E0, $E0, $E0, $E0, $E0, $E0
pbspr:  db $01, $02, $06
        db $FF, $FF, $FF, $FF, $FF, $FF, $FF, $FF, $FF, $FF, $FF, $FF
ibspr:  db $01, $02, $06
        db $FC, $FC, $FC, $FC, $FC, $FC, $FC, $FC, $FC, $FC, $FC, $FC
fxfire: db $E1, $E6, $89, $24, $16, $6C, $D5, $ED, $9F, $34, $10, $55, $C1, $F1, $B8, $4B
        db $10, $3C, $A6, $ED, $D0, $6A, $18, $25, $84, $E0, $E4, $8E, $2C, $15, $5E, $C5
        db $EE, $B5, $4D, $12, $39, $9E, $E9, $D7, $79, $21, $1D, $6F, $CF, $EB, $AA, $45
        db $13, $3F, $A3, $E8, $D4, $79, $23, $1D, $6B, $CA, $EB, $B1, $4F, $16, $36, $94
        db $E1, $DD, $8C, $31, $17, $54, $B5, $EA, $C8, $6B, $20, $23, $72, $CC, $E8, $B1
        db $52, $18, $32, $8A, $DA, $E2, $9C, $40, $17, $41, $9D, $E1, $D9, $8B, $35, $19
        db $4E, $A9, $E4, $D2, $80, $2E, $1C, $56, $B0, $E5, $CD, $7A, $2C, $1E, $5A, $B3
        db $E5, $CB, $79, $2C, $1E, $59, $B1, $E4, $CD, $7D, $30, $1E, $54, $AA, $E1, $D2
        db $87, $37, $1C, $4A, $9F, $DD, $D8, $94, $42, $1C, $3E, $8E, $D4, $DF, $A6, $53
        db $20, $30, $79, $C6, $E2, $BA, $69, $29, $25, $60, $B1, $E0, $CD, $86, $3A, $1F
        db $45, $94, $D4, $DC, $A5, $56, $23, $2E, $71, $BD, $E0, $C4, $7A, $35, $21, $4C
        db $99, $D5, $D9, $A3, $56, $24, $2F, $6D, $B8, $DE, $C8, $83, $3D, $21, $43, $8B
        db $CC, $DD, $B2, $69, $2E, $27, $58, $A2, $D7, $D5, $9E, $55, $26, $30, $6A, $B2
        db $DB, $CC, $8E, $48, $24, $38, $78, $BC, $DC, $C4, $83, $41, $24, $3F, $80, $C1
        db $DB, $BF, $7D, $3E, $25, $42, $84, $C3, $DB, $BE, $7D, $3E, $26, $42, $82, $C0
        db $DA, $C0, $81, $42, $26, $3E, $7B, $BB, $D9, $C5, $8A, $4A, $28, $38, $70, $B1
        db $D6, $CC, $98, $56, $2C, $30, $60, $A2, $D0, $D3, $A8, $68, $35, $2B, $4F, $8D
        db $C4, $D7, $BB, $7F, $44, $2A, $3D, $73, $B1, $D4, $CB, $9A, $5C, $31, $2F, $58
        db $95, $C7, $D5, $B6, $7C, $44, $2B, $3E, $73, $AE, $D2, $CC, $9F, $63, $36, $2E
        db $50, $8A, $BF, $D4, $BF, $8B, $52, $2F, $35, $60, $9A, $C8, $D2, $B4, $7C, $47
        db $2D, $3C, $6C, $A5, $CC, $CE, $AB, $73, $41, $2E, $42, $73, $AB, $CE, $CC, $A6
        db $6E, $3F, $2F, $44, $76, $AC, $CE, $CB, $A5, $6F, $40, $2F, $44, $74, $A9, $CC
        db $CC, $A9, $74, $44, $30, $40, $6D, $A2, $C8, $CD, $B0, $7E, $4C, $32, $3A, $61
        db $96, $C1, $CF, $BA, $8C, $59, $37, $35, $54, $85, $B5, $CD, $C4, $9E, $6B, $41
        db $32, $45, $70, $A3, $C6, $CC, $B1, $82, $52, $36, $38, $59, $8A, $B6, $CC, $C2
        db $9D, $6C, $43, $34, $44, $6C, $9D, $C2, $CB, $B7, $8C, $5C, $3B, $36, $4F, $7B
        db $AA, $C7, $C8, $AC, $7F, $52, $38, $3A, $58, $85, $B1, $C9, $C4, $A5, $77, $4D
        db $37, $3D, $5D, $8A, $B4, $C9, $C2, $A2, $75, $4C, $38, $3E, $5E, $8A, $B3, $C8
        db $C2, $A3, $77, $4F, $39, $3D, $5B, $86, $AF, $C6, $C4, $A8, $7E, $55, $3C, $3B
        db $54, $7D, $A7, $C2, $C6, $B0, $89, $5F, $41, $39, $4B, $70, $9A, $BB, $C7, $BA
        db $98, $6E, $4B, $3A, $42, $5F, $88, $AE, $C4, $C2, $A9, $82, $5B, $40, $3B, $4E
        db $72, $9B, $BA, $C5, $B9, $9A, $72, $4F, $3C, $41, $5A, $81, $A7, $C0, $C3, $B1
        db $8E, $67, $48, $3C, $46, $63, $8A, $AD, $C2, $C1, $AA, $87, $61, $45, $3D, $4A
        db $68, $8E, $B0, $C2, $BF, $A8, $84, $5F, $45, $3D, $4B, $69, $8E, $AF, $C1, $BE
        db $A9, $86, $62, $47, $3E, $49, $65, $8A, $AB, $BF, $C0, $AD, $8D, $69, $4C, $3F
        db $46, $5E, $81, $A3, $BA, $C1, $B3, $97, $74, $54, $42, $42, $54, $74, $96, $B3
        db $C0, $BA, $A4, $83, $61, $49, $40, $4A, $64, $85, $A5, $BA, $BF, $B1, $96, $74
        db $56, $44, $43, $53, $70, $91, $AE, $BD, $BC, $A9, $8B, $6B, $50, $42, $46, $5A
        db $78, $98, $B2, $BE, $B9, $A4, $86, $66, $4D, $43, $48, $5D, $7B, $9B, $B3, $BD
        db $B7, $A2, $84, $66, $4E, $43, $49, $5D, $7A, $99, $B1, $BC, $B7, $A4, $88, $69
        db $51, $44, $48, $5A, $75, $94, $AD, $BA, $B9, $A9, $8F, $71, $57, $47, $46, $53
        db $6C, $8A, $A5, $B7, $BB, $B0, $99, $7D, $61, $4D, $45, $4D, $60, $7C, $99, $AF
        db $BA, $B7, $A6, $8C, $70, $57, $48, $47, $54, $6B, $87, $A2, $B4, $BA, $B2, $9E
        db $83, $68, $52, $47, $4A, $59, $72, $8E, $A6, $B5, $B8, $AE, $99, $7F, $64, $51
        db $48, $4C, $5C, $74, $8F, $A7, $B5, $B7, $AD, $99, $7F, $65, $51, $49, $4C, $5B
        db $73, $8D, $A4, $B4, $B7, $AF, $9C, $83, $69, $55, $4A, $4B, $58, $6D, $87, $9F
        db $B0, $B7, $B2, $A2, $8B, $71, $5B, $4D, $4A, $52, $65, $7D, $95, $A9, $B4, $B5
        db $AA, $96, $7D, $66, $54, $4B, $4D, $5A, $6F, $87, $9E, $AE, $B5, $B1, $A3, $8D
        db $75, $5F, $50, $4B, $51, $60, $76, $8D, $A2, $B0, $B4, $AE, $9E, $88, $71, $5D
        db $50, $4C, $53, $63, $78, $8F, $A3, $B0, $B4, $AD, $9D, $88, $71, $5D, $50, $4D
        db $53, $62, $77, $8D, $A1, $AF, $B3, $AE, $A0, $8C, $75, $61, $53, $4D, $51, $5E
        db $71, $87, $9C, $AB, $B2, $B0, $A4, $92, $7D, $68, $58, $4F, $4F, $59, $69, $7E
        db $93, $A4, $AF, $B1, $AA, $9C, $88, $73, $61, $54, $4F, $53, $5F, $71, $86, $99
        db $A8, $B0, $AF, $A6, $96, $82, $6E, $5D, $52, $50, $55, $63, $75, $89, $9C, $A9
        db $B0, $AE, $A4, $94, $80, $6D, $5D, $53, $50, $56, $63, $75, $89, $9B, $A9, $AF
        db $AE, $A4, $95, $82, $6F, $5F, $54, $51, $56, $61, $72, $85, $97, $A5, $AD, $AE
        db $A7, $99, $88, $75, $64, $58, $52, $54, $5D, $6B, $7D, $90, $A0, $AA, $AE, $AA
        db $A0, $90, $7E, $6D, $5E, $55, $52, $57, $63, $73, $85, $96, $A4, $AC, $AD, $A7
        db $9B, $8B, $79, $68, $5B, $54, $54, $5A, $66, $76, $88, $98, $A5, $AC, $AC, $A5
        db $99, $89, $78, $68, $5B, $55, $55, $5B, $67, $76, $88, $97, $A4, $AB, $AB, $A5
        db $9A, $8B, $7A, $6A, $5E, $56, $55, $5A, $64, $73, $84, $93, $A1, $A9, $AB, $A7
        db $9E, $90, $80, $70, $62, $59, $55, $58, $60, $6D, $7C, $8C, $9A, $A5, $AA, $A9
        db $A3, $97, $88, $79, $6A, $5E, $57, $56
fxhit:  db $A6, $78, $9D, $83, $98, $9C, $69, $85, $B2, $79, $8A, $7F, $A2, $BA, $A0, $7E
        db $60, $84, $A8, $C8, $98, $9E, $AE, $88, $82, $8B, $80, $56, $98, $AC, $6B, $6D
        db $88, $A6, $98, $85, $B1, $CA, $98, $89, $79, $9E, $7E, $A8, $80, $9A, $79, $8F
        db $7D, $64, $7B, $AA, $BD, $D3, $DF, $A2, $A8, $77, $7A, $59, $5E, $5B, $76, $A0
        db $7E, $57, $75, $4E, $36, $4F, $6A, $54, $3D, $2C, $62, $47, $43, $57, $44, $66
        db $80, $AC, $A5, $8D, $86, $8D, $86, $59, $3B, $75, $56, $40, $30, $79, $5E, $63
        db $48, $56, $84, $83, $5D, $6F, $8A, $A0, $AF, $7A, $A4, $81, $9A, $A0, $79, $6C
        db $7B, $9D, $AC, $81, $69, $48, $7F, $8F, $AC, $A0, $96, $66, $5E, $49, $89, $76
        db $66, $48, $88, $B0, $B8, $91, $87, $70, $71, $93, $68, $73, $70, $93, $7E, $82
        db $62, $57, $43, $47, $72, $8C, $A3, $86, $94, $96, $98, $8E, $85, $AA, $70, $70
        db $64, $8A, $81, $80, $8A, $7F, $85, $A3, $85, $A6, $89, $60, $8A, $7E, $6B, $68
        db $95, $70, $81, $77, $5D, $5B, $46, $48, $3F, $32, $4F, $87, $77, $6D, $94, $6D
        db $70, $87, $A3, $79, $97, $81, $9B, $93, $85, $63, $5D, $49, $4E, $4E, $5D, $8D
        db $7C, $8E, $A5, $8E, $7D, $97, $76, $78, $72, $7F, $5F, $81, $78, $73, $5B, $8F
        db $67, $52, $88, $87, $92, $85, $76, $9C, $B5, $9E, $AF, $93, $87, $6D, $88, $7C
        db $82, $9D, $AC, $B3, $91, $97, $A6, $9A, $7C, $65, $6A, $84, $87, $A7, $7E, $5E
        db $6A, $81, $68, $8E, $AA, $9A, $AD, $91, $AA, $A1, $A2, $96, $81, $88, $9B, $94
        db $95, $7F, $95, $AE, $B3, $83, $78, $85, $99, $70, $60, $5D, $61, $4A, $57, $47
        db $70, $68, $8F, $69, $6C, $82, $71, $77, $81, $92, $A5, $B6, $9E, $9C, $9E, $84
        db $64, $81, $7A, $71, $6F, $7B, $79, $6F, $82, $8D, $7E, $94, $92, $A9, $AE, $BA
        db $8B, $77, $5B, $63, $73, $8C, $A8, $97, $AB, $93, $78, $67, $5D, $8C, $A2, $9E
        db $8F, $8B, $9B, $A3, $83, $6B, $61, $7C, $95, $93, $9C, $82, $65, $8E, $84, $9A
        db $99, $AA, $A9, $82, $6F, $8C, $6E, $75, $82, $8C, $73, $7C, $65, $90, $6B, $5C
        db $5A, $7B, $6A, $7F, $7F, $91, $8A, $A3, $82, $83, $78, $67, $80, $9E, $9B, $A1
        db $7E, $89, $74, $89, $9C, $78, $93, $8D, $7B, $81, $89, $A1, $7B, $76, $7F, $81
        db $8D, $8F, $81, $99, $A9, $92, $77, $85, $79, $6B, $71, $59, $86, $90, $8E, $9E
        db $82, $7F, $6F, $76, $7D, $98, $82, $95, $A2, $7D, $9B, $93, $98, $95, $9F, $96
        db $91, $8C, $8E, $A3, $9B, $A5, $80, $9D, $7F, $9B, $99, $98, $7A, $87, $98, $76
        db $88, $76, $66, $82, $7C, $8D, $83, $6C, $8D, $9F, $78, $85, $8C, $76, $5E, $7C
        db $62, $88, $6D, $90, $8C, $76, $79, $66, $8A, $71, $8D, $7F, $67, $84, $74, $7D
        db $88, $7A, $85, $82, $79, $6C, $65, $60, $6C, $5A, $75, $93, $96, $99, $79, $94
        db $9D, $83, $90, $76, $7A, $7C, $6B, $69, $85, $9B, $7A, $78, $63, $70, $80, $8C
        db $79, $7C, $86, $89, $79, $68, $5C, $62, $59, $7D, $91, $7F, $8E, $9A, $A4, $AD
        db $AC, $AB, $9E, $A0, $A0, $A7, $A1, $AA, $AC, $96, $86, $6D, $5F, $74, $84, $91
        db $A3, $AE, $A8, $92, $73, $74, $63, $58, $61, $5C, $5C, $62, $7A, $6A, $6E, $87
        db $73, $88, $71, $85, $7D, $7E, $72, $8C, $9F, $81, $94, $9C, $7B, $89, $91, $86
        db $70, $7C, $6A, $89, $87, $6F, $64, $68, $61, $68, $75, $63, $77, $8B, $75, $85
        db $81, $72, $84, $82, $71, $6A, $6F, $71, $72, $73, $7F, $7C, $90, $85, $95, $A2
        db $9F, $9E, $90, $78, $71, $80, $8B, $70, $8B, $72, $83, $83, $7E, $94, $9C, $89
        db $82, $8A, $75, $7C, $8A, $8D, $7D, $89, $9B, $7E, $8D, $9C, $83, $70, $66, $82
        db $90, $97, $9F, $9C, $97, $96, $89, $93, $87, $73, $7E, $8B, $75, $85, $79, $89
        db $7A, $92, $98, $84, $79, $69, $63, $7B, $7C, $93, $8A, $8D, $9E, $7C, $7F, $89
        db $77, $76, $8E, $83, $79, $88, $87, $79, $7C, $7C, $8F, $86, $97, $99, $95, $80
        db $73, $71, $77, $72, $70, $6D, $79, $67, $75, $6C, $6B, $88, $73, $65, $84, $80
        db $8B, $73, $8D, $96, $7C, $73, $87, $93, $8A, $8F, $90, $7D, $90, $8E, $94, $85
        db $7B, $8B, $8F, $99, $9E, $92, $78, $79, $8B, $82, $82, $7D, $72, $69, $71, $74
        db $8B, $90, $9B, $9F, $A5, $96, $84, $71, $65, $62, $5C, $72, $76, $8D, $7F, $7A
        db $6D, $7B, $72, $66, $5D, $59, $65, $60, $6D, $82, $77, $70, $81, $77, $82, $84
        db $94, $7D, $90, $7E, $8C, $85, $91, $8C, $97, $81, $7C, $73, $6E, $63, $72, $6A
        db $6C, $75, $8A, $77, $81, $74, $6A, $60, $7B, $6C, $87, $92, $85, $70, $67, $73
        db $80, $71, $6E, $79, $86, $88, $79, $8C, $7B, $7C, $7F, $90, $7B, $71, $66, $68
        db $73, $76, $6F, $6D, $7E, $8C, $78, $82, $89, $80, $89, $7F, $6F, $65, $75, $74
        db $6A, $65, $6E, $85, $8C, $96, $93, $9B, $8D, $8B, $84, $78, $85, $7D, $6D, $80
        db $75, $81, $8F, $83, $85, $81, $82, $89, $88, $7C, $6F, $75, $87, $8E, $90, $8E
        db $90, $97, $97, $96, $8D, $8E, $98, $89, $94, $91, $7B, $78, $80, $83, $7C, $6F
        db $75, $73, $77, $83, $89, $96, $8C, $8D, $99, $91, $93, $82, $87, $94, $9A, $98
        db $86, $7A, $7D, $85, $7A, $7A, $87, $83, $84, $8B, $89, $8B, $8E, $80, $8B, $92
        db $7F, $7E, $8C, $8E, $97, $9B, $82, $8A, $81, $7B, $84, $91, $93, $95, $9A, $97
        db $88, $78, $76, $6E, $70, $6F, $73, $82, $86, $8A, $8D, $7B, $76, $86, $81, $7C
        db $82, $76, $8A, $94, $7D, $82, $87, $92, $9A, $86, $88, $7D, $71, $83, $7D, $86
        db $8A, $88, $8C, $8F, $86, $8C, $88, $94, $86, $83, $82, $86, $88, $8F, $7E, $7B
        db $8B, $8B, $78, $77, $85, $75, $78, $6D, $6F, $6D, $7C, $8C, $8D, $8F, $8A, $8B
        db $88, $80, $72, $6F, $7A, $7E, $7A, $6F, $69, $7B, $75, $73, $7E, $78, $76, $70
        db $6F, $76, $73, $80, $75, $71, $74, $7F, $83, $78, $74, $73, $78, $79, $82, $80
        db $77, $88, $7E, $76, $6E, $7F, $82, $7E, $8A, $7A, $76, $86, $8D, $82, $80, $82
        db $7E, $81, $8C, $86, $7B, $71, $7F, $75, $6D, $7E, $88, $78, $81, $80, $83, $86
        db $80, $7B, $81, $7F, $84, $85, $8A, $82, $82, $7A, $84, $7A, $7A, $83, $7D, $72
        db $7F, $7C, $78, $88, $8C, $8E, $8B, $84, $8B, $85, $84, $8C, $8B, $8D, $86, $8B
        db $88, $85, $83, $83, $85, $80, $7B, $88, $87, $90, $8C, $7D, $87, $89, $88, $8A
        db $80, $79, $76, $7B, $73, $72, $6C, $6B, $6A, $70, $6B, $70, $83, $7E, $89, $86
        db $85, $80, $76, $72, $7A, $7B, $80, $81, $75, $77, $73, $6F, $7E, $83, $7F, $76
        db $74, $7B, $73, $80, $76, $7B, $82, $7A, $7D, $82, $76, $6F, $7F, $7E, $7F, $84
        db $89, $7B, $73, $84, $7C, $85, $7F, $88, $8B, $8F, $93, $8D, $85, $8A, $89, $8D
        db $88, $86, $7B, $73, $6D, $6B, $6C, $71, $76, $73, $80, $81, $7A, $78, $7A, $82
        db $76, $71, $76, $77, $86, $87, $84, $7C, $79, $81, $87, $86, $8C, $8E, $8C, $84
        db $7A, $84, $8D, $89, $8F, $94, $89, $7C, $75, $7F, $85, $8B, $7C, $74, $7F, $85
        db $7B, $81, $78, $7D, $78, $80, $8B, $82, $88, $8E, $87, $7E, $7A, $85, $7E, $77
        db $79, $74, $70, $71, $6F, $81, $7F, $81, $7D, $7B, $7A, $84, $7F, $77, $73, $75
        db $75, $75, $82, $82, $79, $86, $83, $85, $80, $85, $86, $8C, $8A, $90, $86, $85
        db $7B, $76, $7E, $88, $7C, $74, $78, $85, $8B, $8E, $83, $84, $84, $7B, $83, $82
        db $7E, $89, $82, $80, $77, $71, $79, $84, $7E, $7C, $77, $76, $7B, $85, $7E, $85
        db $86, $7D, $7D, $81, $89, $8B, $80, $83, $7C, $7F, $81, $85, $7B, $7E, $76, $72
        db $7E, $78, $74, $7C, $80, $84, $79, $81, $80, $80, $84, $83, $7E, $80, $83, $82
        db $80, $89, $89, $84, $7C, $7B, $79, $73, $80, $7A, $80, $83, $8B, $80, $81, $83
        db $87, $8D, $87, $83, $80, $7C, $84, $7E, $84, $7E, $82, $7B, $76, $7E, $81, $7A
        db $7C, $83, $7E, $7F, $79, $84, $86, $85, $8B, $89, $8A, $8B, $8C, $8F, $82, $82
        db $78, $7D, $87, $81, $7E, $80, $7D, $7E, $85, $7C, $78, $7B, $76, $76, $7E, $7C
        db $7C, $82, $7E, $7C, $81, $81, $7C, $85, $88, $88, $88, $84, $7B, $7C, $7D, $78
        db $75, $79, $76, $7F, $78, $81, $86, $7B, $7B, $7A, $7E, $7D, $7E, $82, $80, $80
        db $7E, $77, $83, $80, $7C, $75, $80, $82, $7B, $80, $81, $81, $85, $87, $7F, $88
        db $7E, $85, $7D, $78, $78, $75, $78, $75, $75, $77, $80, $7F, $84, $82, $85, $88
        db $84, $84, $7E, $85, $87, $7D, $7D, $7D, $77, $7E, $7F, $7C, $79, $79, $75, $77
        db $7F, $7A, $7D, $79, $81, $85, $82, $7B, $76, $7A, $76, $77, $77, $82, $7D, $7F
        db $83, $86, $7F, $83, $89, $7E, $78, $76, $7A, $75, $76, $75, $74, $76, $77, $81
        db $7B, $7F, $7C, $7E, $78, $78, $76, $7E, $7D, $81, $87, $88, $7E, $7A, $7E, $83
        db $7C, $7C, $83, $87, $7E, $85, $7D, $82, $7E, $81, $86, $82, $7C, $7E, $7E, $84
        db $84, $86, $83, $83, $88, $7E, $82, $7C, $7A, $80, $7C, $76, $80, $7D, $7E, $84
        db $7D, $81, $81, $82, $7F, $83, $85, $8A, $86, $81, $85, $89, $84, $80, $79, $81
        db $86, $85, $87, $86, $84, $86, $89, $84, $83, $87, $86, $88, $8B, $88, $86, $86
        db $7F, $82, $82, $80, $83, $85, $82, $88, $86, $7E, $81, $81, $82, $7B, $7C, $82
        db $7B, $83, $7E, $7B, $82, $7E, $84, $85, $86, $89, $85, $87, $8A, $84, $82, $85
        db $83, $80, $83, $83, $83, $83, $82, $7E, $7B, $82, $7B, $7F, $83, $7F, $83, $84
        db $85, $87, $81, $7B, $80, $7E, $7D, $7F, $7E, $79, $78, $78, $7C, $7A, $80, $7B
        db $83, $7C, $7A, $7F, $82, $87, $83, $84, $7F, $7A, $77, $75, $77, $7B, $79, $7C
        db $79, $80, $85, $80, $7E, $7C, $80, $84, $81, $7F, $83, $7F, $7B, $82, $80, $7D
        db $84, $7F, $7D, $7E, $7C, $7E, $7A, $79, $78, $7F, $7D, $7E, $7D, $79, $80, $7F
        db $83, $85, $86, $82, $82, $85, $7F, $7E, $79, $79, $76, $77, $7C, $83, $86, $88
        db $81, $7F, $82, $83, $7D, $84, $81, $86, $87, $88, $87, $88, $87, $82, $81, $84
        db $82, $7C, $83, $83, $7C, $7F, $7D, $82, $7D, $7D, $7F, $82, $84, $87, $7E, $84
        db $7D, $7B, $7F, $7B, $82, $80, $84, $7E, $7B, $79, $80, $7D, $7A, $7D, $7D, $81
        db $85, $85, $83, $84, $7E, $81, $7E, $81, $81, $81, $81, $85, $88, $85, $86, $84
        db $85, $81, $81, $86, $84, $83, $86, $88, $89, $80, $84, $86, $83, $7D, $7E, $83
        db $86, $84, $84, $81, $7C, $80, $7C, $7B

; a small explosion: white noise darkened by a sweeping lowpass and faded by
; a square decay, 1200 bytes at 8kHz, so 150ms
fxboom: db $FA, $2D, $FF, $FF, $4C, $FF, $11, $01, $CB, $F0, $68, $E9, $FD, $31, $87, $E3
        db $76, $8A, $82, $1E, $5F, $7B, $F6, $F0, $9B, $AB, $F2, $BB, $D2, $FF, $FF, $BE
        db $51, $C2, $01, $3F, $DC, $3C, $84, $58, $01, $4C, $CE, $78, $01, $06, $01, $46
        db $5E, $49, $2E, $C5, $9A, $DA, $FF, $0A, $D7, $D5, $C0, $78, $01, $01, $C7, $FF
        db $13, $47, $12, $01, $C5, $83, $7D, $D1, $B9, $F5, $15, $E4, $5B, $28, $01, $01
        db $9D, $EF, $FF, $EC, $AA, $5C, $C3, $FF, $DB, $FF, $81, $CD, $DE, $86, $C4, $F7
        db $9C, $06, $01, $CD, $FF, $E7, $FF, $FF, $BC, $94, $73, $0C, $7D, $9A, $5E, $C3
        db $7E, $2E, $C4, $65, $9B, $38, $39, $C7, $98, $52, $78, $3F, $1D, $D5, $ED, $2E
        db $35, $5A, $7A, $89, $F7, $D4, $A1, $2D, $38, $50, $94, $12, $A2, $FF, $F9, $B9
        db $9A, $EA, $4A, $6B, $EB, $A0, $9A, $14, $52, $E2, $3D, $8A, $73, $27, $78, $17
        db $1A, $31, $2C, $55, $70, $E7, $CB, $52, $BF, $EC, $A4, $17, $25, $1A, $43, $71
        db $DF, $CE, $DF, $63, $C6, $B8, $FA, $DC, $6D, $D8, $BA, $EB, $A0, $A0, $92, $58
        db $D8, $A5, $BF, $DC, $8F, $19, $3E, $A8, $BD, $EC, $CA, $34, $1E, $C2, $35, $0E
        db $84, $3C, $29, $35, $39, $0E, $4D, $30, $49, $09, $03, $0C, $B4, $AB, $2E, $99
        db $6A, $1D, $99, $63, $52, $1C, $51, $58, $74, $D1, $E9, $F8, $81, $45, $60, $59
        db $97, $61, $11, $7E, $3C, $99, $86, $9D, $C6, $AE, $CA, $A3, $D0, $DD, $75, $27
        db $79, $4C, $95, $3F, $1D, $35, $4D, $CB, $B7, $D4, $87, $22, $73, $D1, $E2, $B8
        db $E8, $68, $3E, $34, $2F, $B9, $39, $60, $A5, $B4, $54, $B3, $60, $79, $89, $41
        db $95, $38, $47, $13, $51, $99, $77, $9A, $CD, $4E, $20, $2A, $2B, $4A, $C5, $6B
        db $6A, $33, $6C, $3C, $5C, $AB, $A5, $65, $BD, $60, $CA, $72, $3E, $A4, $DF, $A0
        db $41, $54, $7B, $3E, $60, $AE, $D6, $C0, $AB, $BC, $C3, $E9, $F7, $E8, $AE, $A7
        db $5C, $3D, $76, $A3, $77, $C3, $B2, $57, $B6, $43, $29, $40, $AB, $A3, $B8, $56
        db $BA, $DD, $A0, $4C, $8E, $3B, $66, $6F, $94, $B7, $7F, $94, $C1, $C0, $DB, $A2
        db $47, $71, $6E, $45, $23, $48, $3D, $83, $4D, $BA, $C1, $8A, $CD, $77, $96, $5D
        db $82, $A7, $4E, $3D, $30, $92, $98, $4F, $A2, $92, $97, $D0, $F0, $C0, $C2, $A7
        db $62, $33, $40, $31, $87, $CB, $71, $87, $5A, $84, $90, $8B, $BB, $C2, $75, $85
        db $C4, $E8, $D2, $BC, $A8, $BD, $C7, $7B, $74, $AC, $64, $5D, $47, $94, $82, $5A
        db $89, $C4, $E3, $DE, $68, $3E, $8C, $6E, $56, $40, $6A, $3C, $2A, $29, $2E, $92
        db $7E, $84, $84, $89, $A7, $79, $6D, $6B, $AB, $9F, $C3, $AF, $83, $7E, $46, $30
        db $93, $5B, $3E, $9D, $8F, $9E, $94, $6F, $3C, $70, $84, $92, $4A, $90, $C8, $B6
        db $98, $8D, $58, $82, $46, $4A, $93, $86, $77, $B5, $99, $C7, $BB, $6C, $A3, $BD
        db $C4, $73, $45, $57, $41, $33, $24, $80, $4F, $93, $98, $C2, $65, $33, $52, $9A
        db $51, $9F, $C9, $71, $53, $7E, $A0, $67, $AF, $D5, $A0, $67, $74, $4B, $90, $A3
        db $63, $50, $63, $50, $35, $26, $19, $45, $4E, $31, $46, $4C, $4E, $49, $3C, $23
        db $24, $39, $42, $25, $26, $1E, $69, $54, $67, $50, $63, $59, $A3, $6B, $78, $7E
        db $47, $63, $92, $AD, $C5, $A7, $75, $63, $38, $52, $76, $90, $8A, $83, $B1, $B0
        db $79, $9A, $6A, $80, $B3, $8D, $B9, $89, $93, $6B, $95, $87, $7B, $77, $60, $46
        db $88, $5A, $35, $47, $7E, $78, $A7, $B8, $6C, $8F, $AC, $A9, $C3, $C8, $D8, $B2
        db $68, $82, $9F, $7E, $B3, $C3, $79, $66, $6F, $70, $A3, $92, $8F, $7E, $67, $87
        db $51, $84, $60, $68, $94, $6A, $74, $92, $63, $3D, $88, $82, $67, $6B, $78, $92
        db $B0, $A4, $B2, $CA, $9E, $BC, $B0, $7B, $79, $79, $7E, $52, $49, $50, $8B, $7E
        db $91, $9D, $AE, $90, $70, $63, $49, $67, $52, $43, $30, $26, $38, $50, $51, $3F
        db $7F, $68, $56, $40, $7E, $A1, $75, $98, $9D, $69, $58, $4D, $4B, $7B, $80, $5E
        db $90, $B0, $C5, $AA, $C0, $AE, $7A, $59, $4A, $5C, $57, $46, $7A, $79, $69, $61
        db $62, $8F, $97, $66, $5F, $8D, $65, $79, $76, $77, $73, $97, $A1, $8D, $93, $A5
        db $95, $97, $7A, $93, $6B, $66, $4F, $87, $60, $7A, $6A, $62, $85, $9D, $B6, $A1
        db $7C, $7C, $81, $8F, $A3, $89, $80, $8D, $69, $93, $9A, $6E, $7F, $7D, $72, $50
        db $69, $7F, $8D, $61, $80, $5C, $85, $99, $A9, $B6, $B4, $B1, $B1, $98, $A7, $86
        db $6E, $66, $67, $8F, $98, $8B, $A3, $7D, $A1, $85, $9E, $92, $8D, $6F, $5A, $51
        db $41, $3B, $4D, $62, $7A, $89, $9A, $9D, $B8, $80, $61, $68, $4E, $7D, $59, $75
        db $79, $86, $71, $97, $90, $8E, $7F, $9E, $79, $8E, $91, $A2, $A0, $86, $7C, $60
        db $47, $6F, $71, $86, $82, $78, $5C, $7A, $81, $8B, $6C, $7A, $5E, $86, $8B, $83
        db $93, $79, $77, $6B, $58, $4A, $6A, $62, $7C, $63, $73, $72, $76, $71, $97, $79
        db $78, $78, $7A, $66, $8A, $A4, $8D, $A4, $AD, $A9, $81, $72, $5F, $5D, $63, $8A
        db $A7, $A5, $87, $91, $96, $92, $A2, $B3, $84, $6E, $57, $58, $75, $78, $84, $7A
        db $64, $8C, $6A, $5C, $75, $8F, $79, $84, $65, $5A, $6D, $82, $78, $88, $A4, $85
        db $75, $76, $6F, $6A, $6F, $89, $A0, $A0, $89, $8E, $A6, $AB, $85, $92, $7E, $81
        db $68, $88, $79, $8C, $9C, $9B, $9C, $99, $A1, $94, $AA, $B3, $9D, $9E, $94, $A4
        db $B3, $BC, $B4, $BF, $B1, $93, $73, $91, $80, $86, $6E, $85, $99, $88, $84, $9E
        db $8B, $88, $9F, $97, $A1, $8E, $87, $6A, $67, $75, $87, $7C, $8E, $7A, $64, $5C
        db $5F, $57, $61, $77, $6D, $7E, $90, $84, $92, $99, $96, $96, $92, $7B, $96, $7A
        db $80, $67, $6B, $61, $75, $79, $74, $86, $9B, $80, $8A, $97, $87, $7A, $77, $93
        db $93, $83, $70, $7C, $71, $7A, $6A, $87, $96, $A5, $9F, $9C, $7D, $88, $76, $70
        db $6B, $6A, $62, $77, $85, $8F, $9F, $9B, $8D, $86, $9A, $85, $6C, $62, $81, $77
        db $80, $74, $8A, $79, $75, $8C, $76, $81, $7C, $73, $77, $90, $85, $7E, $6F, $6C
        db $5E, $71, $81, $70, $82, $73, $85, $95, $83, $80, $97, $A6, $AC, $A7, $89, $70
        db $63, $6C, $63, $70, $67, $66, $63, $71, $81, $7F, $7F, $75, $81, $72, $6D, $83
        db $8F, $80, $82, $80, $70, $70, $6A, $67, $6E, $81, $86, $8C, $9B, $93, $A0, $AC
        db $90, $96, $88, $77, $7F, $8D, $89, $96, $A3, $A0, $A3, $9C, $99, $99, $9A, $A8
        db $AF, $9E, $AC, $A2, $9E, $AA, $A1, $94, $9D, $A8, $93, $80, $6D, $64, $6E, $75
        db $84, $7E, $82, $92, $9F, $8A, $7A, $89, $7C, $86, $96, $90, $7E, $82, $73, $73
        db $6F, $84, $83, $85, $87, $93, $82, $84, $80, $77, $6E, $6C, $67, $5E, $59, $71
        db $71, $7E, $6C, $70, $6D, $79, $7E, $83, $82, $7C, $83, $92, $90, $87, $81, $7F
        db $8C, $94, $8E, $92, $89, $76, $7C, $70, $6E, $7D, $71, $67, $7D, $8C, $8E, $8E
        db $9C, $8F, $98, $9B, $90, $97, $95, $A2, $A1, $A1, $98, $90, $93, $98, $8C, $94
        db $83, $8D, $99, $8E, $85, $7E, $81, $8D, $82, $8E, $98, $89, $82, $83, $8B, $88
        db $7C, $6D, $80, $8E, $8C, $8B, $80, $83, $86, $8C, $8C, $8F, $95, $84, $7D, $79
        db $8A, $89, $88, $7C, $75, $80, $77, $6C, $62, $71, $71, $79, $78, $84, $84, $8E
        db $88, $8F, $86, $7F, $7F, $8E, $86, $93, $99, $96, $9D, $8F, $8C, $88, $8B, $95
        db $9C, $93, $82, $74, $79, $6D, $74, $81, $83, $78, $77, $6C, $72, $6F, $74, $6D
        db $65, $72, $7D, $82, $8D, $85, $7E, $7F, $8D, $7F, $8A, $93, $87, $93, $96, $9D
        db $A2, $99, $96, $89, $8D, $7E, $88, $82, $89, $8E, $85, $86, $78, $6E, $78, $78
        db $78, $76, $75, $7A, $82, $7E, $84, $89, $8D, $97, $8C, $89, $92, $8E, $97, $9E
        db $95, $96, $9D, $A2, $91, $8C, $8D, $83, $82, $8B, $7F, $73, $81, $87, $7E, $88
        db $92, $91, $8C, $88, $8F, $83, $80, $77, $82, $88, $89, $82, $7F, $79, $6F, $79
        db $7F, $77, $70, $6F, $7B, $76, $7A, $7E, $8A, $7F, $7D, $75, $6E, $7C, $7D, $76
        db $6C, $6F, $7B, $84, $8D, $88, $7F, $81, $81, $7C, $83, $84, $79, $85, $8A, $8E
        db $88, $82, $7E, $80, $82, $8B, $92, $8D, $88, $82, $8A, $8A, $83, $88, $7D, $7B
        db $7E, $87, $85, $7E, $7D, $7D, $7E, $7D, $81, $80, $79, $84, $7B, $77, $7A, $84
        db $7E, $87, $7F, $78, $7E, $74, $7E, $86, $87, $8A, $85, $84, $7F, $76, $77, $70
        db $6E, $67, $6B, $71, $6A, $76, $71, $79, $79, $7D, $75, $73, $6F, $73, $74, $78
        db $72, $6C, $69, $6F, $7A, $7D, $7B, $81, $7C, $86, $84, $87, $8A, $7F, $7A, $76
        db $7C, $84, $7C, $7D, $7E, $87, $82, $7F, $78, $7F, $7B, $79, $77, $78, $78, $76
        db $7B, $83, $7B, $78, $72, $79, $77, $75, $71, $78, $7D, $80, $7B, $7D, $81, $88
        db $7F, $83, $86, $80, $84, $87, $84, $86, $88, $8E, $85, $81, $86, $87, $84, $81
        db $7F, $7E, $81, $86, $86, $88, $83, $87, $82, $83, $87, $81, $86, $8B, $8A, $8E
        db $8D, $87, $7E, $7C, $7D, $76, $77, $73, $71, $74, $76, $6F, $6D, $6B, $67, $72
        db $6F, $72, $74, $72, $74, $71, $70, $6F, $6C, $6B, $69, $6F, $76, $73, $79, $7F
        db $85, $7D, $7F, $81, $7C, $76, $76, $76, $7E, $79, $78, $79, $7F, $7D, $83, $86
        db $80, $82, $80, $85, $85, $82, $7C, $77, $7A, $76, $7A, $77, $76, $75, $7A, $75
        db $77, $79, $78, $7D, $7C, $77, $7D, $7B, $7A, $76, $77, $79, $79, $80, $7B, $7C
        db $7F, $7D, $7B, $82, $7E, $79, $76, $7B, $7F, $83, $89, $8A, $8A, $8B, $8B, $8F
        db $90, $92, $8F, $90, $8E, $89, $8A, $8F, $8C, $86, $82, $80, $82, $7C, $82, $85
        db $7F, $79, $7F, $79, $7F, $84, $86, $89, $84, $89, $82, $82, $88, $87, $87, $83
        db $84, $85, $80, $82, $85, $86, $85, $82, $87, $87, $88, $84, $80, $7F, $7C, $7B
        db $7D, $82, $85, $85, $82, $7F, $83, $87, $81, $86, $84, $83, $84, $85, $86, $87
        db $82, $87, $87, $86, $84, $81, $80, $7F, $82, $82, $87, $82, $84, $83, $7D, $79
        db $7C, $7A, $79, $7E, $81, $82, $84, $87, $83, $82, $80, $7E, $7C, $7E, $7A, $7C
        db $80, $85, $83, $88, $8B, $8A, $85, $89, $85, $81, $82, $81, $7F, $83, $86, $83
        db $81, $83, $84, $81, $7E, $7F, $84, $81, $81, $84, $81, $82, $81, $84, $87, $8A
        db $8A, $88, $89, $89, $88, $8B, $8E, $8B, $8A, $86, $83, $81, $84, $84, $80, $83
        db $81, $80, $83, $86, $89, $87, $84, $84, $80, $82, $83, $85, $83, $80, $80, $7D
        db $7C, $7C, $7C, $80, $7F, $7F, $7F, $7C, $7B, $77, $75, $73, $76, $74, $77, $7B
        db $7F, $82, $82, $81, $81, $82, $7F, $7B, $78, $7B, $7E, $80, $7E, $80, $80, $82
        db $81, $82, $80, $7E, $80, $7E, $80, $83, $82, $82, $85, $83, $81, $82, $84, $84
        db $85, $85, $83, $85, $83, $83, $83, $81, $7E, $7E, $81, $83, $80, $7D, $7F, $7F
        db $80, $7D, $81, $80, $7E, $7E, $80, $82, $83, $84, $86, $84, $84, $87, $86, $86
        db $83, $84, $83, $84, $83, $82, $82, $80, $81, $84, $82, $82, $7F, $82, $84, $86
        db $87, $88, $88, $8A, $89, $8B, $88, $85, $87, $85, $83, $83, $84, $84, $85, $84
        db $82, $81, $7F, $7E, $80, $7D, $7B, $7C, $7D, $80, $7E, $7E, $7C, $7B, $7C, $7B
        db $7C, $7A, $78, $7A, $79, $78, $78, $79, $7B, $7A, $7A, $78, $77, $76, $78, $77
        db $79, $7B, $7B, $79, $7B, $7B, $7D, $7F, $7E, $7F, $7D, $7E, $80, $7F, $80, $80
        db $7F, $81, $7F, $80, $82, $83, $82, $83, $83, $81, $80, $80, $7E, $7D, $7E, $7D
        db $7F, $7E, $7E, $7C, $7A, $7C, $7A, $79, $7B, $79, $78, $79, $77, $78, $79, $79
        db $79, $78, $77, $78, $79, $7A, $7B, $7D, $7F, $80, $80, $82, $83, $84, $84, $86
        db $85, $83, $85, $86, $86, $83, $83, $83, $84, $83, $83, $84, $84, $83, $84, $84
        db $84, $86, $87, $87, $85, $86, $86, $87, $87, $85, $83, $82, $83, $83, $85, $85
        db $83, $83, $85, $83, $84, $83, $83, $81, $82, $83, $82, $81, $81, $83, $84, $82
        db $84, $82, $84, $84, $86, $86, $86, $88, $85, $86, $86, $86, $84, $84, $84, $82
        db $82, $81, $80, $82, $82, $83, $83, $82, $81, $82, $82, $82, $82, $83, $85, $85
        db $86, $87, $86, $87, $86, $86, $84, $84, $83, $83, $81, $82, $81, $82, $82, $81
        db $80, $81, $80, $80, $82, $80, $7F, $80, $80, $7F, $80, $80, $81, $82, $82, $84
        db $83, $82, $83, $83, $83, $83, $82, $82, $83, $84, $82, $81, $82, $82, $81, $82
        db $81, $80, $81, $81, $82, $83, $84, $82, $82, $82, $82, $81, $81, $82, $81, $82
        db $80, $81, $81, $80, $80, $80, $7E, $80, $80, $81, $81, $82, $83, $82, $83, $82
        db $81, $82, $83, $83, $82, $81, $82, $81, $82, $82, $81, $81, $81, $81, $81, $82
        db $81, $81, $81, $80, $7F, $7F, $80, $81, $80, $81, $80, $80, $80, $81, $80, $80
        db $7F, $7F, $7E, $7E, $7E, $7D, $7D, $7D, $7D, $7E, $7E, $7E, $7E, $7E, $7F, $7F
        db $7F, $80, $80, $7F, $7F, $7E, $7F, $7F, $7F, $80, $80, $81, $81, $80, $80, $80
        db $80, $81, $81, $80, $80, $7F, $7F, $7E, $7F, $7E, $7E, $7D, $7D, $7C, $7D, $7E
        db $7E, $7D, $7D, $7E, $7E, $7F, $7E, $7E, $7E, $7F, $7F, $7F, $7F, $80, $80, $80
        db $80, $80, $81, $81, $80, $81, $82, $81, $81, $80, $80, $7F, $7F, $7E, $7F, $7E
        db $7F, $7F, $7F, $80, $80, $80, $80, $80, $80, $80, $80, $80, $7F, $80, $80, $80
        db $80, $80, $80, $80, $7F, $80, $7F, $7F, $80, $7F, $7F, $7F, $7E, $7E, $7E, $7E
        db $7F, $7F, $7F, $7F, $7F, $7E, $7F, $7F, $80, $80, $7F, $7F, $7F, $7E, $7E, $7F
        db $7F, $7F, $7F, $7F, $7F, $7F, $7F, $7F, $7F, $80, $7F, $80, $80, $80, $80, $80
        db $80, $80, $7F, $7F, $7F, $7F, $7F, $80, $80, $7F, $7F, $7F, $7F, $7F, $7F, $7F
        db $7F, $7F, $7F, $7F, $7F, $7F, $7F, $7F, $80, $7F, $7F, $7F, $7F, $7F, $7F, $80
        db $80, $7F, $7F, $7F, $80, $7F, $7F, $80, $80, $80, $7F, $80, $80, $80, $80, $80
        db $80, $80, $81, $81, $81, $80, $81, $81, $81, $81, $81, $81, $81, $81, $81, $81
        db $81, $81, $81, $81, $81, $81, $81, $81, $81, $81, $81, $81, $81, $81, $81, $81
        db $81, $81, $81, $81, $81, $80, $80, $81, $81, $81, $81, $81, $81, $80, $80, $80
        db $80, $80, $80, $81, $81, $81, $80, $80, $80, $80, $80, $80, $80, $80, $80, $80
        db $80, $80, $80, $80, $80, $80, $80, $80, $80, $80, $80, $80, $80, $80, $80, $80
        db $80, $80, $80, $80, $80, $80, $80, $80, $80, $80, $80, $80, $80, $80, $80, $80
        db $80, $80, $80, $80, $80, $80, $80, $80, $80, $80, $80, $80, $80, $80, $80, $7F
        db $80, $80, $80, $80, $80, $80, $80, $80, $80, $80, $80, $80, $80, $80, $80, $80
        db $80, $80, $80, $80, $80, $80, $80, $80, $80, $80, $80, $80, $80, $80, $80, $80
        db $80, $80, $80, $80, $80, $80, $80, $80, $80, $80, $80, $80, $80, $80, $80, $80
        db $80, $80, $80, $80, $80, $80, $80, $80, $80, $80, $80, $80, $80, $80, $80, $80
        db $80, $80, $80, $80, $80, $80, $80, $80, $80, $80, $80, $80, $80, $80, $80, $80
        db $80, $80, $80, $80, $80, $80, $80, $80, $80, $80, $80, $80, $80, $80, $80, $80
        db $80, $80, $80, $80, $80, $80, $80, $80
