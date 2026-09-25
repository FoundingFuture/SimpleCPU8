; PAC-MAN. Four mazes from MazeMan, cycled per level. This task copies one
; maze out of ROM into RAM with one CMD_COPY, paints it, then fades the
; three maze palette entries up from black, so the draw is never seen
; happening.
;
; The program is three screens in a ring: the attract screen, the game and
; the game over screen. Power on lands on the attract screen. A key starts a
; game and the last life leads to game over. A key there goes back to the
; attract screen. Nothing halts: the machine runs until the host quits.
; DESIGN: the work that happens ONCE sits above `attract`, and the work that
; happens once per GAME sits in newgame. The split is what makes a second
; game start clean. The palette map, the level table, the row tables, the
; sprites and the samples describe the machine and never change. The score,
; the lives, the level pointer and the maze describe a game. Those are written
; again for every one.

; Pac-Man's second sprite, drawn over the ghosts. See paclayer.
.equ PACTOP, 6
; The half shades the maze is anti-aliased with: the wall's, and the one
; dots and pills share. Palette entries the fades write beside 1 to 3, so no
; sprite colour may sit on them either. 4 between them is the pupils.
.equ WALLDIM, 5
.equ DOTDIM, 6

; --- silence first, before anything else runs. The audio chip survives a
; Reset, and so does data RAM, so a Reset pressed while the siren is looping
; would otherwise leave it droning over the whole of the redraw the Reset
; starts. Restart powers the chip on and needs no help; this line is for Reset,
; which does not.
        JSR sndoff

; --- the palette lives in RAM. A palette is 768 bytes, so it moves through
; memory rather than a byte at a time through a port. Point the GPU at the
; buffer once, dump the power-on palette into it, and from here on a colour
; change is an ordinary store followed by one command.
        OUT GPU_MAP, MAP_PALETTE
        OUT GPU_MAP_HI, palbuf >> 8
        OUT GPU_MAP_LO, palbuf & 255
        OUT GPU_CMD, CMD_MEMMAP
        OUT GPU_CMD, CMD_STORE_PALETTE

; --- the maze palette. Index 0 is the background and stays black.
        JSR foblack

; --- entry 4, the ghosts' pupils. A sprite colour, not a maze colour, and it
; is written here because the GPU's own default for index 4 is a dark green.
; DESIGN: the pupils live at 4 rather than at 3 because the maze OWNS 1, 2 and
; 3 and rewrites all three on every fade. At $03 a pupil was painted the power
; pill's pale peach and vanished into the white of the eye. No sprite colour
; may sit in 1 to 3; pacman-sprites.test.ts holds every strip to that.
; DESIGN: index 0 is transparent, so true black cannot be a sprite pixel at
; all. 24 is as near black as a drawn pixel gets.
; DESIGN: here, at the top of the program, and nowhere else. fput, fisnap and
; foblack each write exactly three entries from index 1, so nothing in the
; fade reaches 4. What does reach it is Restart, which powers the GPU on with
; the default palette back, and Restart re-runs the program from this line.
        LD D1 <- palbuf+12
        LD A <- 24
        LD [D1]+ <- A
        LD [D1]+ <- A
        LD [D1] <- A
        OUT GPU_CMD, CMD_FETCH_PALETTE

; --- the score strip, in the eight screen rows above the maze. The overlay
; rides over graphics and sprites, so it costs no video memory and no sprite.
; The maze's fades never touch it. $DB is the default palette's light grey,
; rgb(6,6,3) in 3-3-2, which is outside the entries 1 to 3 the maze rewrites.
; Grey rather than Pac-Man's yellow, so the strip reads as the frame and the
; board keeps the colour. The style is set by the screen that owns the
; overlay: newgame sets the strip's grey and prints the strip, and the attract
; screen sets its own white. The game over screen keeps the strip up and
; prints its prompt in the same grey. hud prints the strip again whenever a
; value on it changes.

; --- the per-level difficulty table, out of the cartridge and into RAM.
; DESIGN: the CPU cannot read the cartridge, so the GPU moves the bytes, the
; same one-cycle CMD_COPY loadmaze uses for a maze. It runs once, before the
; first draw, because drawmaze reads the row for Pac-Man's speed and msdur
; reads it for the scatter duration. The pointer into it is per game and is
; set in newgame.
; DESIGN: lvltab lives past work, not with the scalars. It is 126 bytes and
; the zero page has 68 to spare; nothing reads it with [addr8] anyway,
; because lvlptr walks it, exactly as rowtab, p2t and sqtab are walked.
        OUT GPU_CART_BANK, get_bankbyte(lvlrom)
        OUT GPU_CART_HI, get_highbyte(lvlrom)
        OUT GPU_CART_LO, get_lowbyte(lvlrom)
        OUT GPU_DEST_HI, get_highbyte(lvltab)
        OUT GPU_DEST_LO, get_lowbyte(lvltab)
        OUT GPU_LEN_HI, 0
        OUT GPU_LEN_LO, 147
        OUT GPU_CMD, CMD_COPY

; --- the two row tables. Both hold addresses and neither reads a maze, so
; they are built once for every game the machine will play.
        JSR mkrows
        JSR mkhrows

; --- Pac-Man, sprite 1, a 12 frame strip, from pacspr. The five sprites are
; created hidden: the attract screen shows them when its chase begins, and
; newgame shows them under the fade-in of the first maze.
        OUT GPU_SPRITE, 1
        OUT GPU_SRC_BANK, get_bankbyte(pacspr)
        OUT GPU_SRC_HI, get_highbyte(pacspr)
        OUT GPU_SRC_LO, get_lowbyte(pacspr)
        OUT GPU_CMD, CMD_SPRITE_DEF
; --- sprite 6 is Pac-Man again, above the ghosts. The GPU draws sprites in
; id order, so sprite 1 is under the ghosts at 2 to 5 and sprite 6 over
; them. The draw moves and animates the two together, and paclayer shows the
; upper one only while no hunting ghost overlaps him. So a hunting ghost is
; drawn over Pac-Man, and Pac-Man over a frightened ghost or a pair of eyes.
        OUT GPU_SPRITE, PACTOP
        OUT GPU_SRC_BANK, get_bankbyte(pacspr)
        OUT GPU_SRC_HI, get_highbyte(pacspr)
        OUT GPU_SRC_LO, get_lowbyte(pacspr)
        OUT GPU_CMD, CMD_SPRITE_DEF

; --- the four ghosts, sprites 2 to 5. Each strip is eight frames, two per
; facing, so a ghost's frame is its direction times two plus the skirt.
; DESIGN: OVERRIDE. A loop over ghsttab, where this was four near-identical
; blocks. get_bankbyte and its two siblings resolve at assembly time, so a
; strip's cartridge address is a constant baked into the OUT that carries it
; and there is nothing to index at runtime, which is why the four were written
; out. The power pill changed that arithmetic: art that changes at runtime has
; to reach a ghost's own strip from a ghost NUMBER, and that is the wall
; loadmaze met and answered with mazetab. The table exists now, so the startup
; shares it rather than keeping a second spelling of the same four addresses.
; DESIGN: this is the last strip write outside the draw, and it is not part of
; the state-and-art pairing adstrip owns. It exists to CREATE the five sprites,
; because CMD_SPRITE_SHOW needs a sprite to show. adstrip corrects whatever it
; leaves on the first frame drawn, astrip shipping as 0 for exactly that.
        LD A <- 1
        LD [t3] <- A
gsinit: LD A <- [t3]
        JSR ghstrip
        LD A <- [t3]
        INC A
        LD [t3] <- A
        SUB A <- 5
        JNZ gsinit
gsdone:
; --- the seven samples reach the chip, once. The three held ones are given
; their loop flag here too; see sndinit.
        JSR sndinit

; =========================== the attract screen ===========================
; The title, the roster and the chase, looping until a key or a button is
; pressed. vsync paces it on the GPU frame counter, the same wait the game
; uses. The screen therefore runs at the game's own sixty frames a second.
; DESIGN: the five game sprites ARE the attract screen's actors, drawn by the
; game's own actdraw from the actor window. The attract writes ax, ay, adir
; and astate and calls the draw, the same as the walk does. That buys the
; mouth animation, the four facings, the blue strip and the strip cache for
; free. The roster is stamped rather than shown. Five sprites cannot stand in
; the roster and run the chase at once. CMD_STAMP bakes the sprite
; into video memory and leaves the sprite free.
; DESIGN: any key or button starts the game, not only fire. A person at a
; title screen presses whatever is under a finger. A screen that ignores the
; wrong key reads as a hang. anykey answers both the key queue and the pad.
; drainkeys empties both before the screen begins waiting. A key held over
; from the previous screen therefore cannot skip this one.
attract: JSR drainkeys
        JSR atsetup
atloop: JSR vsync
        JSR anykey
        JNZ newgame
atstay: JSR atstep
        JSR atblink
        JMP atloop

; =========================== a new game ===========================
; Everything a game owns starts here, and nothing a game owns starts anywhere
; else. The maze, the actors, the mode clock and the fright are per level and
; drawmaze resets those; this block resets what is per GAME and then draws
; level 1 exactly as nextlvl draws level 2.
; DESIGN: the strip cache astrip is NOT reset. It says what each sprite is
; wearing, and the attract screen kept it true through actdraw. The first walk
; of the game corrects the four ghosts back from blue. It is the compare that
; corrects them after a pill. Zeroing it would define five strips for
; nothing.
; DESIGN: the sprites are placed by drawall before they are shown, and shown
; before the fade-in. Shown first, all five would sit where the attract chase
; left them until the walk ran. Placed after the fade, the maze would arrive
; with no one on it. nextlvl learnt the same order; see the note there.
newgame: LD D1 <- score
        LD A <- 0
        LD [D1] <- A
        LD [D1+1] <- A
        LD [gstate] <- A
        LD [dietk] <- A
        LD [ghchain] <- A
        LD [pacstl] <- A
        LD [pacph] <- A
        LD [pacpd] <- A
        LD [pactk] <- A
        LD [wakaph] <- A
        LD A <- 3
        LD [lives] <- A
        LD A <- 1
        LD [level] <- A
        LD [lvlno] <- A
        LD D1 <- lvltab
        LD [lvlptr] <- D1
        LD A <- 20
        LD [lvlrem] <- A     ; rows left to advance through before the last
        OUT GPU_CMD, CMD_TEXT_CLEAR
        OUT GPU_TEXT_COLOR, $DB  ; the strip's light grey, see the note at the top
        OUT GPU_CMD, CMD_TEXT_STYLE
        JSR hud
        JSR loadmaze
        JSR drawmaze
        JSR drawall
        JSR showall
        JSR fadein
; The board is up, so the game is playing rather than ready. gstate is 0
; from the top of this block until this line, which is the whole of the draw
; and the fade-in.
        LD A <- 1
        LD [gstate] <- A

; =========================== main loop ===========================
; DESIGN: the frame's work is chosen by gstate, and only two of its four
; values do anything here. 1 is playing: step everyone, age the mode clock,
; then ask whether a ghost caught him. 2 is dying: the pause counts down and
; nothing else runs, so no actor steps and no sprite moves. 3 is game over,
; and it leaves the loop: see the arm itself. 0 (ready) cannot be seen from
; here, gstate being 1 before main is ever entered and never going back.
; DESIGN: contact runs AFTER actall, not inside it. It reads the sprites the
; GPU holds, and actall is what moves them to where the records say; asking
; before the walk would measure the previous frame's positions. It also means
; nothing is mid-walk when a death is declared, which is what lets resetact
; run the walk itself without saving acti, aastep and acur the way nextlvl
; has to.
main:   JSR vsync
; --- the held sound, once a frame and outside the gstate dispatch, because
; every state has a bed and in three of the four it is silence. See sndbed.
        JSR sndbed
        LD A <- [gstate]
        CMP A, 1
        JZ mainplay
        CMP A, 2
        JZ maindie
; Game over, and nothing leaves state 3 from inside the game. The game over
; screen takes over from here. sndbed above has already dropped the bed to
; silence on this same pass.
; DESIGN: the exit belongs here and not in dieall. dieall runs inside the
; actor walk, several returns deep. Leaving there would abandon the frame it
; was in half finished. Reaching it from the dispatch means the death frame
; completed like any other.
        JMP gameover
mainplay: JSR actall
        JSR modestep
        JSR contact
        JMP main
maindie: JSR diestep
        JMP main

; =========================== game over ===========================
; The maze fades to black under the five hidden sprites. Then the screen is
; cleared and GAME OVER goes up in block letters, with the score strip still
; above it. A key or a button returns to the attract screen.
; DESIGN: block letters through bigtext, because the GPU's font has one size.
; Two text rows reading GAME OVER would be lost in a 256 pixel screen. The
; arcade's own game over is the loudest thing on its board.
; DESIGN: the fade runs first and the keys are drained after it. A key pressed
; during the fade is dropped too. The wait begins with an empty queue and the
; pad's current level, whatever the player did while he died.
gameover: JSR hideall
        JSR fadeout
        OUT GPU_COLOR, 0
        OUT GPU_CMD, CMD_CLEAR
        OUT GPU_COLOR, $E0   ; the default palette's red
        OUT GPU_CMD, CMD_SET_COLOR
        LD A <- 82
        LD [bgx] <- A
        LD A <- 76
        LD [bgy] <- A
        LD D1 <- biggame
        JSR bigtext
        LD A <- 82
        LD [bgx] <- A
        LD A <- 120
        LD [bgy] <- A
        LD D1 <- bigover
        JSR bigtext
        OUT GPU_TEXT_COL, 15
        OUT GPU_TEXT_ROW, 21
        OUT GPU_CMD, CMD_TEXT_AT
        LD D1 <- gopress
        JSR puts
        JSR drainkeys
gowait: JSR vsync
        JSR anykey
        JZ gowait
        JMP attract

; --- wait for the GPU's frame counter to move, then remember it.
; DESIGN: IN sets no flags, so the SUB is what tests the counter. The frame
; loop, the attract screen and the game over screen all pace on this one
; routine and one byte. Leaving one screen for another therefore never waits
; a frame twice or skips one.
vsync:  IN GPU_FRAME
        CMP A, [mainfr]
        JZ vsync
        LD [mainfr] <- A          ; CMP kept the frame in A
        RET

; --- nonzero in A when a fresh press has arrived since the last call.
; DESIGN: two sources, and they are read differently because they are
; different things. IO_KEY is a queue of events. A press is an event with bit
; 7 clear, and a release is skipped over to reach the next one. The pad is a
; level. A press there is a bit that is set now and was not set at the last
; call. The level test is what makes a held button harmless. drainkeys records
; the level on entry to a screen. A button still down from the previous screen
; then changes nothing until it is let go and pressed again.
anykey: IN IO_KEY -> A
        LD [t3] <- A
        TST A, $7F
        JZ akpad             ; the queue is empty: ask the pad
        LD A <- [t3]
        TST A, $80
        JZ akyes             ; bit 7 clear: a press
        JMP anykey           ; a release: look at the next event
akpad:  IN IO_CONTROLLER -> A
        LD [t3] <- A
        XOR A <- [padprev]
        AND A <- [t3]        ; set now and not before: a fresh press
        LD [t4] <- A
        LD A <- [t3]
        LD [padprev] <- A
        LD A <- [t4]
        RET
akyes:  LD A <- 1
        RET

; --- empty the key queue and take the pad's level as the baseline anykey
; compares against. Called on entry to a screen that waits for a key.
drainkeys: IN IO_KEY -> A
        OR A <- 0            ; IN sets no flags
        JNZ drainkeys
dkpad:  IN IO_CONTROLLER -> A
        LD [padprev] <- A
        RET

; --- show or hide all five sprites. One loop, with the choice in t4: 1
; shows and 0 hides. A command name is an OUT operand and not a value the
; CPU can hold. The loop therefore branches on the flag rather than carrying
; a byte.
; DESIGN: Pac-Man's upper copy, sprite 6, is hidden here and never shown:
; paclayer decides it on every draw, and only while pacvis says sprite 1 is
; up, so the copy never outlives the sprite it copies.
showall: LD A <- 1
        LD [pacvis] <- A
        JMP spall
hideall: LD A <- 0
        LD [pacvis] <- A
        OUT GPU_SPRITE, PACTOP
        OUT GPU_CMD, CMD_SPRITE_HIDE
spall:  LD [t4] <- A
        LD A <- 1
        LD [t3] <- A
spnext: LD A <- [t3]
        OUTA GPU_SPRITE
        LD A <- [t4]
        JZ sphide
        OUT GPU_CMD, CMD_SPRITE_SHOW
        JMP spstep
sphide: OUT GPU_CMD, CMD_SPRITE_HIDE
spstep: LD A <- [t3]
        INC A
        LD [t3] <- A
        SUB A <- 6
        JNZ spnext
spdone: RET

; --- print the zero terminated RAM string D1 points at, at the text cursor.
; D1 comes back pointing past the terminator, so packed strings can be walked
; one call at a time; the roster does exactly that.
puts:   LD A <- [D1]+
        JZ psdone
        OUTA GPU_TEXT_CHAR
        OUT GPU_CMD, CMD_TEXT_CHAR
        JMP puts
psdone: RET

; --- draw a string of block letters. bgx and bgy are the top left corner
; and D1 points at the string. The pen colour is the letters' colour. A letter
; is a 5 by 7 bitmap drawn as 4 by 4 pixel blocks, one CMD_RECT per set bit.
; So a letter is 20 by 28 pixels, and a column advances by 24.
; DESIGN: the string holds OFFSETS into bigfont rather than letter numbers,
; seven per letter. The CPU has no multiply, and the string is data anyway.
; The string ends on 255, which no offset can be.
; DESIGN: the bits are walked by doubling. Bit 4 is the leftmost column. One
; AND tests it, and one add to itself brings the next column under it.
bigtext: LD A <- [D1]+
        LD [bgptr] <- D1
        LD [bgg] <- A
        SUB A <- 255
        JZ bgdone
        LD A <- 0
        LD [bgr] <- A
        LD A <- [bgy]
        LD [bgpy] <- A
bgrow:  LD A <- [bgg]
        ADD A <- [bgr]
        LD D2 <- bigfont
        LD A <- [D2+A]
        LD [bgbits] <- A
        LD A <- 0
        LD [bgc] <- A
        LD A <- [bgx]
        LD [bgpx] <- A
bgcol:  LD A <- [bgbits]
        TST A, $10
        JZ bgskip
        OUT GPU_X_HI, 0
        LD A <- [bgpx]
        OUTA GPU_X
        OUT GPU_Y_HI, 0
        LD A <- [bgpy]
        OUTA GPU_Y
        OUT GPU_CMD, CMD_MOVE_TO
        OUT GPU_X_HI, 0
        LD A <- [bgpx]
        ADD A <- 3
        OUTA GPU_X
        OUT GPU_Y_HI, 0
        LD A <- [bgpy]
        ADD A <- 3
        OUTA GPU_Y
        OUT GPU_CMD, CMD_RECT
bgskip: LD A <- [bgbits]
        SHL A
        LD [bgbits] <- A
        LD A <- [bgpx]
        ADD A <- 4
        LD [bgpx] <- A
        LD A <- [bgc]
        INC A
        LD [bgc] <- A
        CMP A, 5
        JNZ bgcol
bgnext: LD A <- [bgpy]
        ADD A <- 4
        LD [bgpy] <- A
        LD A <- [bgr]
        INC A
        LD [bgr] <- A
        CMP A, 7
        JNZ bgrow
bgglyph: LD A <- [bgx]
        ADD A <- 24
        LD [bgx] <- A
        LD D1 <- [bgptr]
        JMP bigtext
bgdone: RET

; --- the attract screen from a clean slate: the title, no roster yet, the
; five sprites hidden and wearing their own colours. Phase 0 and tick 0.
; DESIGN: the five actdraw calls put the ghosts back into their own strips
; after the chase turned them blue. They go through the same window and the
; same cache the game uses. They also leave every sprite at the top left,
; which nobody sees because all five are hidden.
atsetup: JSR hideall
        LD A <- 0
        LD [mzxleft] <- A    ; no maze under the chase: screen pixels, see mzxleft
        LD [acti] <- A
        LD [ax] <- A
        LD [adir] <- A
        LD A <- 1
        LD [astate] <- A
        LD A <- 194
        LD [ay] <- A
asloop: JSR actdraw
        LD A <- [acti]
        INC A
        LD [acti] <- A
        CMP A, 5
        JNZ asloop
asdrawn: OUT GPU_COLOR, 0
        OUT GPU_CMD, CMD_CLEAR
        OUT GPU_CMD, CMD_TEXT_CLEAR
        OUT GPU_TEXT_COLOR, $FF
        OUT GPU_CMD, CMD_TEXT_STYLE
        OUT GPU_COLOR, $FC   ; Pac-Man's own yellow for the title
        OUT GPU_CMD, CMD_SET_COLOR
        LD A <- 46
        LD [bgx] <- A
        LD A <- 12
        LD [bgy] <- A
        LD D1 <- bigttl
        JSR bigtext
        LD A <- 0
        LD [atph] <- A
        LD [attk] <- A
        LD A <- 8
        LD [atrow] <- A
        LD D1 <- atnames
        LD [atnptr] <- D1
        LD A <- 255          ; neither blink state, so the first frame prints
        LD [blinkst] <- A
        RET

; --- one frame of the attract screen. atph is the phase and attk the frames
; spent in it. Phases 0 to 4 reveal the roster one actor at a time. 5 is the
; chase to the left and 6 the chase back to the right. 7 is a pause before it
; all runs again.
atstep: LD A <- [atph]
        CMP A, 5
        JZ atleft
        CMP A, 6
        JZ atright
        CMP A, 7
        JZ atpause
; the roster: on the phase's first frame stamp the actor and print its name,
; then hold for 40 frames.
        LD A <- [attk]
        JZ atrnew
        INC A
        LD [attk] <- A
        SUB A <- 40
        JZ atnext
        RET
atnext: LD A <- 0
        LD [attk] <- A
        LD A <- [atph]
        INC A
        LD [atph] <- A
        RET
; DESIGN: the roster stamps sprite phase plus 2 for a ghost and sprite 1 for
; Pac-Man. His frame 1 is the half open mouth facing right. The ghosts are at
; frame 0 from atsetup, facing right too. The row is a text row, so the stamp
; lands at eight times it less three. That centres a 14 pixel sprite on an 8
; pixel line of text.
atrnew: LD A <- [atph]
        CMP A, 4
        JZ atrpac
        ADD A <- 2
        JMP atrst
atrpac: LD A <- 1
        LD [aspr] <- A
        JSR adframe
        LD A <- 1
atrst:  OUTA GPU_SPRITE
        OUT GPU_SPRITE_X_HI, 0
        OUT GPU_SPRITE_X, 71
        OUT GPU_SPRITE_Y_HI, 0
        LD A <- [atrow]
        LD [t3] <- A
        ADD A <- [t3]
        LD [t3] <- A
        ADD A <- [t3]
        LD [t3] <- A
        ADD A <- [t3]        ; eight times the row
        SUB A <- 3
        OUTA GPU_SPRITE_Y
        OUT GPU_CMD, CMD_STAMP
        OUT GPU_TEXT_COL, 16
        LD A <- [atrow]
        OUTA GPU_TEXT_ROW
        OUT GPU_CMD, CMD_TEXT_AT
        LD D1 <- [atnptr]
        JSR puts
        LD [atnptr] <- D1
        LD A <- [atrow]
        ADD A <- 3
        LD [atrow] <- A
        LD A <- 1
        LD [attk] <- A
        RET
; the chase to the left. Pac-Man leads from x 184 with the four ghosts 16
; pixels apart behind him, all at a pixel a frame. He stands on the pill at x
; 20 on frame 164. The pill is drawn on the first frame and eaten on the last.
; DESIGN: positions are computed from the tick rather than stepped. A phase is
; then a function of attk, and the two chases cannot drift apart. The ghosts
; are hidden one by one on the way back. The first frame of this phase shows
; all five again.
atleft: LD A <- [attk]
        JNZ atlgo
atlnew: JSR showall
        OUT GPU_COLOR, $FE   ; the pill, in a peach the maze fades never touch
        JSR atpill
atlgo:  LD A <- 2            ; everyone faces left
        LD [adir] <- A
        LD A <- 184
        SUB A <- [attk]
        LD [atpx] <- A
        LD A <- 200
        SUB A <- [attk]
        LD [atx] <- A
        LD A <- 1
        LD [astate] <- A
        JSR atactors
        LD A <- [attk]
        INC A
        LD [attk] <- A
        SUB A <- 165
        JZ atleat
        RET
atleat: OUT GPU_COLOR, 0
        JSR atpill           ; the pill is gone
        LD A <- 6
        LD [atph] <- A
        LD A <- 0
        LD [attk] <- A
        RET
; the chase back. The ghosts are blue and flee to the right at a pixel a
; frame; Pac-Man follows at two and eats each one he reaches, which hides its
; sprite. He leaves the screen at frame 116.
; DESIGN: frtk is held above the flashing threshold, so the blue strip shows
; its plain frame; adfrgh reads it exactly as it does in the game. drawmaze
; resets frtk through unfright before any game reads it.
atright: LD A <- 0
        LD [adir] <- A
        LD A <- [attk]
        SHL A
        ADD A <- 20
        LD [atpx] <- A
        LD A <- [attk]
        ADD A <- 36
        LD [atx] <- A
        LD A <- 2
        LD [astate] <- A
        LD A <- 100
        LD [frtk] <- A
        JSR atactors
        LD A <- [attk]
        INC A
        LD [attk] <- A
        SUB A <- 117
        JZ atrend
        RET
atrend: LD A <- 7
        LD [atph] <- A
        LD A <- 0
        LD [attk] <- A
        RET
; the pause: a second of the empty road, then the whole screen again.
atpause: LD A <- [attk]
        INC A
        LD [attk] <- A
        SUB A <- 60
        JZ atsetup           ; a tail call: it starts over from the title
        RET

; --- draw the five chase actors from atpx, atx, adir and astate. Pac-Man is
; at atpx and the ghosts at atx, 16 pixels apart. A ghost Pac-Man has reached
; is hidden; that is a no-op on the way out, when he is behind all four.
; DESIGN: astate is written for Pac-Man too, and it does not matter. adstrip
; never reads his state byte, and actdraw takes his own arm before the
; frightened test; see the note at adstrip.
atactors: LD A <- 194
        LD [ay] <- A
        LD A <- 0
        LD [acti] <- A
        LD A <- [atpx]
        LD [ax] <- A
        JSR actdraw
        LD A <- 1
        LD [acti] <- A
aaghost: LD A <- [atx]
        LD [ax] <- A
        JSR actdraw
        LD A <- [atx]
        CMP A, [atpx]
        JC aaeaten           ; the ghost is behind him
        JNZ aashown           ; or exactly under him
aaeaten: LD A <- [acti]
        INC A
        OUTA GPU_SPRITE
        OUT GPU_CMD, CMD_SPRITE_HIDE
aashown: LD A <- [atx]
        ADD A <- 16
        LD [atx] <- A
        LD A <- [acti]
        INC A
        LD [acti] <- A
        SUB A <- 5
        JNZ aaghost
aafin:  RET

; --- the power pill of the chase. A disc of radius 3 at the centre of the
; tile Pac-Man stops on. The pen colour is the caller's: peach to draw it and
; the background to eat it.
atpill: OUT GPU_CMD, CMD_SET_COLOR
        OUT GPU_X_HI, 0
        OUT GPU_X, 24
        OUT GPU_Y_HI, 0
        OUT GPU_Y, 206
        OUT GPU_CMD, CMD_MOVE_TO
        OUT GPU_RADIUS, 3
        OUT GPU_CMD, CMD_CIRCLE
        RET

; --- PRESS A KEY TO START, on for 32 frames and off for 32. Bit 5 of the
; GPU's frame counter is the clock. The line is printed only when the bit changes, so a
; frame costs one IN and one compare.
atblink: IN GPU_FRAME
        AND A <- $20
        LD [t3] <- A
        SUB A <- [blinkst]
        JZ abdone
        LD A <- [t3]
        LD [blinkst] <- A
        OUT GPU_TEXT_COL, 11
        OUT GPU_TEXT_ROW, 28
        OUT GPU_CMD, CMD_TEXT_AT
        LD A <- [blinkst]
        JZ abon
        LD D1 <- atblank
        JMP puts             ; a tail call: puts' RET answers the frame loop
abon:   LD D1 <- atpress
        JMP puts
abdone: RET

; --- copy the current level's maze into the working array.
; DESIGN: the CPU cannot read the cartridge, so the GPU moves the bytes, one
; cycle whatever the length. The entry is found by stepping three bytes per
; level: the get_ macros resolve at assembly time, so a runtime index into
; four fixed addresses has to walk a table instead, and there is no
; multiply to turn level into an offset directly. A table of four rows is
; not worth a second lookup table just to skip the walk.
loadmaze:
        LD D1 <- mazetab
        LD A <- [level]
        SUB A <- 1
        LD [t1] <- A
lmstep: LD A <- [t1]
        JZ lmgot
        SUB A <- 1
        LD [t1] <- A
        LD D1 <- D1+3
        JMP lmstep
lmgot:  LD A <- [D1]
        OUTA GPU_CART_BANK
        LD A <- [D1+1]
        OUTA GPU_CART_HI
        LD A <- [D1+2]
        OUTA GPU_CART_LO
        OUT GPU_DEST_HI, get_highbyte(work)
        OUT GPU_DEST_LO, get_lowbyte(work)
        OUT GPU_LEN_HI, 3      ; 868 bytes, high byte first
        OUT GPU_LEN_LO, $64
        OUT GPU_CMD, CMD_COPY
        RET

; --- mzxleft := (256 - mzcols * 8) / 2, the pixel column of tile column 0.
; DESIGN: 128 - mzcols * 4, so the 256 never has to fit in a byte. No
; multiply, so the four is two doublings. Called from drawmaze, because the
; width is the maze's and the offset is per maze, like everything else the
; draw resets. The result is 16 for every shipped maze; see mzxleft.
mzcentre: LD A <- [mzcols]
        SHL A
        LD [t1] <- A
        ADD A <- [t1]        ; mzcols * 4
        LD [t1] <- A
        LD A <- 128
        SUB A <- [t1]
        LD [mzxleft] <- A
        RET

; --- the row table: 31 addresses, each the first byte of a maze row.
; DESIGN: the CPU has no multiply, so row times 28 is a lookup instead.
; Built once at startup by walking forward 28 bytes at a time.
mkrows: LD D1 <- work
        LD D2 <- rowtab
        LD A <- 0
        LD [t1] <- A
mrloop: LD [D2] <- D1        ; store the row address, high byte first
        LD D2 <- D2+2
        LD A <- 28
        LD [t2] <- A
mrstep: INC D1
        LD A <- [t2]
        SUB A <- 1
        LD [t2] <- A
        JNZ mrstep
mrnext: LD A <- [t1]
        INC A
        LD [t1] <- A
        SUB A <- 31
        JNZ mrloop
mrdone: RET

; --- read the tile at (dcol, drow) into A.
tileat: LD A <- [drow]
        SHL A      ; row times 2, the table holds words
        LD D1 <- rowtab
        LD D2 <- [D1+A]      ; D2 := the row's address
        LD A <- [dcol]
        LD A <- [D2+A]
        RET

; --- nonzero when tile (t1, t2) is anything but wall. Out of the grid
; counts as open, so a maze whose edge is wall gets an outer border and a
; maze whose edge is not gets nothing there. mouthdir wants the opposite
; answer outside the grid and reaches the tile test through nbropen, which
; does its own range check and then jumps in here; see nbropen.
; DESIGN: mirrors tileat's rowtab lookup rather than calling tileat itself.
; wallpaint below asks this up to four times per wall tile, once per
; neighbour, and tileat always reads dcol/drow rather than taking a tile
; position as an argument. Routing through it would mean setting
; dcol/drow to the neighbour's position for each call, then restoring
; them before returning to drawmaze's own walk, which dmnext and dmeol
; depend on still holding the tile just drawn. Reading t1/t2 directly
; costs less than that save and restore, and never touches dcol/drow at all.
openat: LD A <- [t2]
        CMP A, 31
        JNC oaopen             ; row 0..30: check the column next
oarow:  LD A <- [t1]
        CMP A, 28
        JC oatile             ; column 0..27 too: safe to read the tile
oaopen: LD A <- 1
        RET
; a marker tile ('P', 'H', '-', or a tunnel digit) reads as open here
; whether or not drawmaze has blanked it to a space yet: the test below is
; "equals '#'", true for a wall and false for anything else, so the order
; drawmaze blanks markers in never matters to this check.
oatile: LD A <- [t2]
        SHL A         ; row times 2, the table holds words
        LD D1 <- rowtab
        LD D2 <- [D1+A]       ; D2 := the row's address
        LD A <- [t1]
        LD A <- [D2+A]
        CMP A, $23          ; '#'
        JZ oawall
        LD A <- 1
        RET
oawall: LD A <- 0
        RET

; --- leave D1 pointing at the tile at (dcol, drow), so a caller can write.
; DESIGN: there is no indexed store, so the column is walked. At most 27
; steps. Called from eat, once per dot or pill, and from blank inside
; drawmaze's own loop, once per marker tile: the two P markers, the two door
; cells, the H, and every tunnel mouth, which is two on CLASSIC and four on
; each of the other three shipped mazes.
tileptr: LD A <- [drow]
        SHL A
        LD D2 <- rowtab
        LD D1 <- [D2+A]
        LD A <- [dcol]
        JZ tpdone
        LD [t2] <- A
tpwalk: INC D1
        LD A <- [t2]
        SUB A <- 1
        LD [t2] <- A
        JNZ tpwalk
tpdone: RET

; --- paint the working array. One pass, one pointer, and the pixel position
; carried along rather than computed.
; DESIGN: a per-tile walk from the start of work, repeated for every one of
; 868 tiles, costs O(rows*cols) work per tile: about 365,000 inner steps and
; roughly 1.4 million instructions, against a 200,000 cycle test budget.
; One pointer incremented once per tile, with px and py carried alongside
; instead of recomputed, makes the whole draw a single linear pass instead.
; Nothing shows here: the three palette entries are black. fadein below
; brings them up once the draw is done.
; DESIGN: the tunnel table is per draw, not per program. tuncount, tuntab and
; tundtb all describe the maze being drawn and nothing else, and tundtb's $FF
; is what tells a pair's first mouth from its second (see dmtun below), so a
; slot left filled from the previous maze reports "already seen" on the new
; maze's first mouth and records that mouth as a partner of the old maze's.
; nextlvl calls drawmaze again for every level, so the clear belongs here.
; Measured with the clear removed: on level 2 slot 0 keeps CLASSIC's row 14
; mouths and takes Parking Lot's row 1 mouths as their partners, which is a
; tunnel between two mazes.
; DESIGN: OVERRIDE. The screen clear lives here too, not at the top of the
; program. drawmaze paints only '#', '.' and 'o' tiles; every space tile is
; left untouched, which was invisible for level 1 only because the program
; cleared the screen once before the first draw ever ran. nextlvl calls
; drawmaze again with the previous maze still in video memory and nothing
; in between to clear it, so a wall or dot that becomes a corridor in the
; new maze would keep showing the old maze's pixels, and fadein would then
; bring them up to full brightness. One clear owned by the routine that
; needs it, run before every draw, beats a second clear duplicated at
; nextlvl that has to remember to agree with this one. CMD_CLEAR fills
; with GPU_COLOR, so color 0 is set first.
; DESIGN: drawmaze owns per-level state. dots, the tunnel table and the
; screen clear all landed here for the same reason, each the first time it
; came up: nextlvl runs drawmaze again for every level clear, so anything
; that must start fresh per maze and gets reset by a caller instead just
; waits for that caller to forget. pacdir, pacnext and pacacc are the third
; instance. Nothing wrote them back to their spawn values, so the direction
; he happened to be holding when the last dot of the old maze was eaten
; carried into the new one and kept driving him with no button pressed at
; all. pacdir and pacnext reset to 3 (up), the same default declared at
; pacdir below and verified against all four mazes' spawn tiles there, not
; only CLASSIC's; pacacc resets to 0 so the sub-pixel phase starts clean
; too. The rule from here on: state that must be fresh once per level, not
; once per program, is reset in this block, never in nextlvl and never in
; dmpac. The ghosts arrived: their records are reset by placeghosts, called
; from the end of this same draw, which is this rule and not an exception.
; DESIGN: pacdir and pacacc are fields of Pac-Man's record now, not
; standalone scalars, and this block still resets them there rather than in
; the window. It has to: drawmaze also runs once at startup, before any actor
; has been loaded into the window at all. The window is a step's workspace and
; the record is the durable home, so a per-level reset belongs in the record.
; nextlvl reloads the window from it straight after this call, which is what
; makes the reset reach the step already in progress -- see the note there.
; DESIGN: the scatter and chase clock is the fifth instance. mode, modetk,
; modetk8 and modefl are all per level: a level that began halfway through a
; chase, on a timer the last maze had run down, would have its ghosts give up
; the hunt at a moment nothing on the new board explains, and a modefl carried
; across would turn all four around on the first frame of it. modetk is loaded
; through msdur rather than written here, so the duration lives in one place.
; DESIGN: the fright window is the sixth, and it is reset through unfright
; rather than by zeroing frtk here, because ending a fright is two things: the
; clock, and the blue strip four sprites are wearing. placeghosts, at the end
; of this same pass, writes every ghost's state and speed and nothing about
; its art, so a maze that changed while a pill was standing would open with
; four blue ghosts hunting normally. See unfright.
; DESIGN: the ghosts have arrived, and they are the fourth instance of the
; same rule. doorx, doory, housex, housey and outdy are all read out of the
; maze being drawn, so all five reset here, and placeghosts at the end of the
; pass puts the four ghosts on this maze's own door and house rather than
; leaving them standing where the previous maze's were. doorx leans on the
; reset twice over: it is both the recorded position and dmdoor's "have we
; recorded one yet" flag, the shape the tunnel latch had before it became a
; table with an emptiness mark of its own. See dmdoor.
drawmaze:
        OUT GPU_COLOR, 0
        OUT GPU_CMD, CMD_CLEAR
        JSR mzcentre         ; where tile column 0 lands, for this maze's width
        LD D1 <- dots
        LD A <- 0
        LD [D1] <- A
        LD [D1+1] <- A
        LD [tuncount] <- A
        LD [doorx] <- A
        LD [doory] <- A
        LD [housex] <- A
        LD [housey] <- A
        LD [outdy] <- A
        LD [pacacc] <- A
        LD [mode] <- A
        LD [modefl] <- A
        LD [modetk8] <- A
        LD A <- 3
        LD [pacdir] <- A
        LD [pacnext] <- A
        JSR lvlspd           ; this level's speeds, before placeghosts wants one
        JSR msdur            ; a full scatter timer for the mode just reset
        JSR unfright         ; and no pill left standing from the last maze
        JSR tunclr
        LD A <- 0
        LD D1 <- work
        LD [drow] <- A
        LD [py] <- A
dmrow:  LD A <- 0
        LD [dcol] <- A
        LD [px] <- A
dmcol:  LD A <- [D1]
        LD [dch] <- A
        SUB A <- $23         ; '#'
        JZ dmwall
        LD A <- [dch]
        CMP A, $2E         ; '.'
        JZ dmdot
        CMP A, $6F         ; 'o'
        JZ dmpill
        CMP A, $50         ; 'P'
        JZ dmpac
; A tunnel mouth is any digit '1' to '9', not just '1'. Three of the four
; shipped mazes declare two pairs, and the second one's tiles used to fall
; through this chain as literal '2' characters: neither wall nor floor, so
; the corridor that ended on one was a dead end no rule recognised. A range
; test, so the digit is also the slot: A holds it zero based by the time
; dmtun is reached.
        SUB A <- $31         ; '1', the low end of the tunnel digits
        JC dmdash            ; below '1': not a digit at all
        LD [tund] <- A       ; the digit, zero based: the slot it records into
        CMP A, 9
        JC dmtun             ; '1' to '9'
dmdash: LD A <- [dch]
        CMP A, $2D         ; '-', one of the two door cells
        JZ dmdoor
        CMP A, $48         ; 'H', the house point
        JZ dmhouse
        JMP dmnext
dmwall: OUT GPU_COLOR, 1
        OUT GPU_CMD, CMD_SET_COLOR
        JSR wallpaint
        JMP dmnext
; a dot or a pill: draw it and count it toward the level's total. bump uses
; D2, never D1: D1 is this loop's own walking pointer through work. blank
; below also calls into a D1-clobbering routine (tileptr) from inside this
; same loop, and survives only because tileptr recomputes rowtab[drow]+dcol,
; bit for bit the walking pointer's current value. bump has no such luck:
; it must point at dots, an address unrelated to the tile being drawn, so
; landing back on the right value by coincidence is not available to it,
; and D2 is the only way to leave D1 alone.
dmdot:  OUT GPU_COLOR, 2
        OUT GPU_CMD, CMD_SET_COLOR
        JSR dotrect
        JSR bump
        JMP dmnext
dmpill: OUT GPU_COLOR, 3
        OUT GPU_CMD, CMD_SET_COLOR
        JSR pillrect
        JSR bump
        JMP dmnext

; --- Pac-Man's start. A later P overwrites an earlier one: CLASSIC's pair
; are adjacent tiles and he starts on the rightmost one's left edge. pacx and
; pacy are the first two bytes of his record, so this writes the record, not
; the window: see the DESIGN note on the reset block above.
; DESIGN: written twice, to the record and to pacsx/pacsy. The record holds
; where he IS, which after a catch is where he died, so a death reset needs a
; copy the game never moves. drawmaze is the one writer of both, so the spawn
; is refreshed for every maze and a level change cannot leave the old maze's
; P standing.
dmpac:  LD A <- [px]
        LD [pacx] <- A
        LD [pacsx] <- A
        LD A <- [py]
        LD [pacy] <- A
        LD [pacsy] <- A
        JSR blank
        JMP dmnext

; --- OVERRIDE: record every pair the maze declares, not the first digit only,
; and record where each mouth leads as well as where it is. tund holds the
; digit zero based, which is the slot. The draw meets tiles in order, so the
; first mouth of a slot is its a half and the second is its b half; tundtb's
; $FF is what tells them apart, and tunclr writes it before every draw.
; The two ends of a pair are NOT "left" and "right" any more. Photo
; Opportunity's second pair runs (27, 4) to (0, 27), so a is the one met
; first, whatever side of the maze it is on.
; DESIGN: mouthdir runs before blank, and clobbers D1 doing it. That is safe
; for exactly the reason blank itself is safe to call from inside this loop:
; blank goes through tileptr, which rebuilds D1 as rowtab[drow] + dcol, bit
; for bit the walking pointer's own value. So blank stays last here.
dmtun:  JSR mouthdir
        LD [tunw] <- A
        JSR tundsl           ; D1 := tundtb + tund * 2
        LD A <- [D1]
        CMP A, $FF
        JZ dmtuna            ; the slot is empty: this is the pair's first mouth
        LD A <- [tunw]       ; the second: the b half of the slot
        LD [D1+1] <- A
        JSR tunsl            ; D1 := tuntab + tund * 4
        LD A <- [px]
        LD [D1+2] <- A
        LD A <- [py]
        LD [D1+3] <- A
        JSR blank
        JMP dmnext
dmtuna: LD A <- [tunw]
        LD [D1] <- A
        JSR tunsl
        LD A <- [px]
        LD [D1] <- A
        LD A <- [py]
        LD [D1+1] <- A
; The count is the highest digit the maze declares. Every shipped maze
; declares '1', and three declare '2' as well, so the slots in use run from 0
; with no hole in them. A maze that skipped a digit would leave one, and the
; tunnel tests read the maze text and would say so.
        LD A <- [tund]
        ADD A <- 1
        LD [tunt] <- A
        CMP A, [tuncount]
        JC dmtunb            ; borrow: the count already covers this slot
        LD A <- [tunt]
        LD [tuncount] <- A
dmtunb: JSR blank
        JMP dmnext

; --- D1 := tuntab + tund * 4, and tundsl the same for tundtb at two bytes a
; slot. There is no add for a D register, so an index is a walk, tileptr's own
; shape. Nine slots, so 32 steps at the very most and 4 on a shipped maze.
tunsl:  LD D1 <- tuntab
        LD A <- [tund]
        SHL A
        LD [tunt] <- A
        ADD A <- [tunt]      ; tund * 4
        JMP tunwalk
tundsl: LD D1 <- tundtb
        LD A <- [tund]
        SHL A      ; tund * 2
tunwalk: JZ tunsldone        ; slot 0: D1 is already there
        LD [tunt] <- A
tunslw: INC D1
        LD A <- [tunt]
        SUB A <- 1
        LD [tunt] <- A
        JNZ tunslw
tunsldone: RET

; --- the direction from the mouth at (dcol, drow) toward its one open
; neighbour inside the grid: 0 right, 1 down, 2 left, 3 up. This is the
; direction an actor leaves that mouth by when it comes out of the tunnel.
; DESIGN: read off the maze, not "carry on the way you were going". Every
; mouth is a dead end -- one open neighbour and no more, which the tunnel
; tests re-derive from the maze text for all fourteen of them -- so the
; direction is well defined, and it is not always the direction of travel.
; Metro Station's second pair sits at the bottom of two vertical corridors,
; entered downward from above; carrying on downward out of the far mouth
; lands on (26, 22), which the maze text has as wall, and an actor centred in
; a wall with every way out shut is exactly the freeze this task removes.
mouthdir: LD A <- 0
        LD [tunt] <- A
mdloop: LD D1 <- dxtab
        LD A <- [tunt]
        LD A <- [D1+A]
        ADD A <- [dcol]
        LD [t1] <- A
        LD D1 <- dytab
        LD A <- [tunt]
        LD A <- [D1+A]
        ADD A <- [drow]
        LD [t2] <- A
        JSR nbropen
        JZ mdnext
        LD A <- [tunt]
        RET
mdnext: LD A <- [tunt]
        ADD A <- 1
        LD [tunt] <- A
        SUB A <- 4
        JNZ mdloop
; No open neighbour at all. No shipped maze declares such a mouth, and the
; tunnel tests check every one. Right keeps the answer a legal direction.
mddone: LD A <- 0
        RET

; --- nonzero when the tile at (t1, t2) is inside the grid and is not wall.
; DESIGN: openat with the opposite answer outside the grid. openat is
; wallpaint's, and wallpaint wants the maze's outer edge to draw a border
; against, so it calls anything off the grid open. A mouth's neighbours are
; asked a different question, which one direction leads somewhere, and off
; the grid leads nowhere. The range check is the whole difference, so the
; in-range case jumps into openat rather than repeating its rowtab lookup.
nbropen: LD A <- [t2]
        CMP A, 31
        JNC nbono            ; row 0..30: check the column next
nbocol: LD A <- [t1]
        CMP A, 28
        JC openat            ; column 0..27 too: openat reads the tile
nbono:  LD A <- 0
        RET

; --- blank the tunnel table for the maze about to be drawn. tuntab goes to
; zero and tundtb to $FF, the "this slot is empty" mark dmtun reads.
tunclr: LD D1 <- tuntab
        LD A <- 36
        LD [tunt] <- A
tclr1:  LD A <- 0
        LD [D1] <- A
        INC D1
        LD A <- [tunt]
        SUB A <- 1
        LD [tunt] <- A
        JNZ tclr1
tclr2:  LD D1 <- tundtb
        LD A <- 18
        LD [tunt] <- A
tclr3:  LD A <- $FF
        LD [D1] <- A
        INC D1
        LD A <- [tunt]
        SUB A <- 1
        LD [tunt] <- A
        JNZ tclr3
tclrdone: RET

; --- the ghost house door, two cells wide. Keep the first one met, which is
; the left one, because the draw walks a row left to right. Verified against
; all four shipped mazes: every one puts its door at columns 13 and 14 of one
; row, so doorx + 8 is the right cell wherever the pair sits.
; doorx doubles as the "have we recorded one yet" flag, the shape the tunnel
; latch had before dmtun above became a table with an emptiness mark of its
; own, which is $FF and not zero. Two facts make doorx safe, and neither is
; "zero is an unlikely value": drawmaze zeroes doorx at the top of every
; draw, so the previous maze's door can never answer for this one, and no
; shipped maze puts a door cell in column 0 (all four use columns 13 and 14),
; so a genuinely recorded doorx is never 0 either.
; NOTE: this keeps the FIRST of the two door cells, where dmpac above keeps
; the LAST of the two P cells. Both are deliberate. The draw meets tiles in
; order, so first is the left door cell and last is the right P cell.
dmdoor: LD A <- [doorx]
        JZ dmdoor1           ; nothing recorded yet: this is the left cell
; Both cells blank to spaces, and that is the finished state, not a stand-in:
; a door character in work would have to be a wall to everyone or open to
; everyone, and the door is neither. canmove is where it becomes a wall to
; Pac-Man and one way to a ghost, and it reads doorx and doory to do it, which
; is why the position is recorded here and why nothing survives in work.
        JSR blank            ; the right cell
        JMP dmnext
dmdoor1: LD A <- [px]
        LD [doorx] <- A
        LD A <- [py]
        LD [doory] <- A
        JSR blank            ; the left cell
        JMP dmnext

; --- the house point, one 'H' per maze. It is a cell inside the house, which
; is what dmdone below reads to work out which side of the door is outside.
dmhouse: LD A <- [px]
        LD [housex] <- A
        LD A <- [py]
        LD [housey] <- A
        JSR blank
        JMP dmnext

; write a space over the marker so movement never sees it
blank:  JSR tileptr
        LD A <- $20
        LD [D1] <- A
        RET

dmnext: INC D1
        LD A <- [px]
        ADD A <- 8
        LD [px] <- A
        LD A <- [dcol]
        INC A
        LD [dcol] <- A
        CMP A, 28
        JNZ dmcol
dmeol:  LD A <- [py]
        ADD A <- 8
        LD [py] <- A
        LD A <- [drow]
        INC A
        LD [drow] <- A
        CMP A, 31
        JNZ dmrow

; --- the pass is over. Work out which side of the door is outside, then put
; the ghosts on this maze.
; DESIGN: the H cell is inside, because the house is what holds it, so
; outside is the other way. Read, never assumed: three of the four shipped
; mazes put the house below the door, and Metro Station puts it above (its
; door is row 22 and its H row 20). Anything that reaches for "the cell above
; the door" finds house interior on that one maze.
; After SUB, carry is borrow, so JC is taken when housey is the smaller of
; the two: the house sits above the door, and outside is downward, +8.
; Falling through means the house is below and outside is upward, -8, which
; is 248 in a byte.
dmdone: LD A <- [housey]
        CMP A, [doory]
        JC dmabove
        LD A <- 248          ; house below the door: outside is up
        JMP dmout
dmabove: LD A <- 8           ; house above the door: outside is down
dmout:  LD [outdy] <- A
; The siren's four thresholds, worked out from the total this pass just
; counted. Here rather than in a caller for the reason the reset block above
; gives: dots is per level, and so is anything measured off it.
        JSR sndlvl
        JSR floodhome        ; the road home, for the maze just laid down
        JSR placeghosts
        RET

; --- put the four ghosts where this maze starts them. Called once per draw,
; so a ghost is never left standing on the previous maze's door cell.
; Pac-Man's own start is not written here: dmpac took his position from the
; maze's P as the walk passed it, and drawmaze's reset block wrote his
; direction and accumulator, so by the time this returns all five actors are
; placed for the maze just drawn.
; DESIGN: writes the records, not the actor window, for the same reason the
; reset block does. drawmaze also runs at startup, before any actor has been
; loaded into the window at all, and the record is an actor's durable home.
; DESIGN: free to use D1. drawmaze's walking pointer through work is spent by
; the time this is called, the pass having ended at dmdone just above. The two
; routines called from inside that loop each have to leave D1 back on the walk
; position, blank by recomputing exactly it through tileptr and wallpaint by
; saving and restoring it; this one is past the loop and has nothing to
; protect. D2 is free for the same reason, and the reason is the same one:
; nothing needs it past the end of the pass. Three routines in the draw load it,
; not one -- openat for its rowtab lookup, tileptr for its own, and bump for the
; dot count -- and every one of them loads it fresh for a single lookup and
; never reads it back. drawmaze's two callers hold nothing in it either: the
; startup sequence has nothing live at all, and nextlvl, which is reached from
; eat through dropdot, touches only D1 the whole way down.
placeghosts:
; --- the release schedule, and the divider it counts on. Per level, like the
; four records below it: placeghosts runs from the end of every drawmaze.
; DESIGN: the delays are chosen, not the arcade's. The arcade releases a ghost
; on a dot counter, which this does not have; these are a plain wait, picked
; far enough apart that the order is watchable rather than a scramble. 8, 20
; and 32 ticks of the divider, which is 64, 160 and 256 frames. Ticks rather
; than frames because a byte of frames tops out at 255 and 256 is already past
; it, so the same three numbers would not fit a byte at all.
; DESIGN: literals here, not a table. A later task moves them into the
; per-level table it brings for the ghost speeds, and until that table exists
; a second one holding three numbers buys nothing.
; DESIGN: the divider phase resets to 1, not to 0. A level change runs this
; from inside some actor's step, and that walk then carries on into the three
; ghosts behind it in the same frame. A phase of 0 is what a countdown reads as
; "this is the eighth frame", so every one of them would lose a tick before it
; had stood for a single frame, and levels 2 upward would run a schedule eight
; frames shorter than level 1's. Measured before the fix: 8, 20 and 32 at the
; first frame loop entry of level 1 against 7, 19 and 31 on levels 2 and 3.
        LD D2 <- houstk
        LD A <- 0
        LD [D2] <- A         ; Pac-Man's entry, never read
        LD [D2+1] <- A       ; Blinky's, never read: he starts outside
        LD A <- 1
        LD [houphs] <- A
        LD A <- 8
        LD [D2+2] <- A       ; Pinky
        LD A <- 20
        LD [D2+3] <- A       ; Inky
        LD A <- 32
        LD [D2+4] <- A       ; Clyde
; and no ghost is owed a normal release. A revive mark left standing over a
; level change or a death would send the next ghost out of the house dangerous
; on a board of blue ones. See revive.
        LD D2 <- revive
        LD A <- 0
        LD [D2] <- A
        LD [D2+1] <- A
        LD [D2+2] <- A
        LD [D2+3] <- A
        LD [D2+4] <- A
; Blinky starts outside, on the left door cell's column, one tile past the
; door on the side outdy just picked. Scatter corner top right.
        LD D1 <- blinky
        LD A <- [doorx]
        LD [D1] <- A
        LD A <- [doory]
        ADD A <- [outdy]
        LD [D1+1] <- A
        LD A <- 1            ; state: normal, already out of the house
        LD [D1+5] <- A
        LD A <- 27
        LD [D1+6] <- A
        LD A <- 0
        LD [D1+7] <- A
        JSR ghcom
; Pinky starts on the H cell itself. Scatter corner top left.
        LD D1 <- pinky
        LD A <- [housex]
        LD [D1] <- A
        LD A <- [housey]
        LD [D1+1] <- A
        LD A <- 0            ; state: still in the house
        LD [D1+5] <- A
        LD [D1+6] <- A
        LD [D1+7] <- A
        JSR ghcom
; Inky one tile left of H. Scatter corner bottom right.
        LD D1 <- inky
        LD A <- [housex]
        SUB A <- 8
        LD [D1] <- A
        LD A <- [housey]
        LD [D1+1] <- A
        LD A <- 0
        LD [D1+5] <- A
        LD A <- 27
        LD [D1+6] <- A
        LD A <- 30
        LD [D1+7] <- A
        JSR ghcom
; Clyde one tile right of H. Scatter corner bottom left.
        LD D1 <- clyde
        LD A <- [housex]
        ADD A <- 8
        LD [D1] <- A
        LD A <- [housey]
        LD [D1+1] <- A
        LD A <- 0
        LD [D1+5] <- A
        LD [D1+6] <- A
        LD A <- 30
        LD [D1+7] <- A
        JSR ghcom
        RET

; the three fields every ghost starts alike, into the record D1 points at.
; DESIGN: the speed comes from ghspd, a scalar, and not from the level table
; directly. D1 is this routine's argument, the record being written, and
; reading the table needs D1 for the pointer. lvlspd reads the row once per
; placement instead, which is also one read for four ghosts.
ghcom:  LD A <- 3            ; dir: up
        LD [D1+2] <- A
        LD A <- 0            ; acc: a clean sub-pixel phase
        LD [D1+3] <- A
        LD A <- [ghspd]      ; spd: this level's ghost speed, from the table
        LD [D1+4] <- A
        RET

; --- define ghost number A's sprite from its own strip. 1 is Blinky and 4 is
; Clyde, the order the records are laid out in, and the sprite is that plus
; one. The selected sprite is left selected, so a caller can go on to show it.
; DESIGN: a table walked three bytes at a time, exactly as loadmaze walks
; mazetab, and for exactly loadmaze's reason: the three get_ macros resolve at
; assembly time, so four fixed addresses picked between at runtime have to be
; bytes in RAM rather than operands. There is no multiply, so the walk is the
; index.
; DESIGN: t1 and t2, and both are free at both call sites. The startup loop
; counts in t3, unfright holds its own state in frg and a D2 pointer, and the
; draw's own users of t1 and t2 are openat and drawmaze's tile walk, which are
; a whole pass away from unfright's call in the reset block above them.
ghstrip: LD [t1] <- A
        LD D1 <- ghsttab
        SUB A <- 1
        JZ ghsgot            ; ghost 1: D1 is already there
        LD [t2] <- A
ghswalk:LD D1 <- D1+3
        LD A <- [t2]
        SUB A <- 1
        LD [t2] <- A
        JNZ ghswalk
; The id goes into DATA0 first, so the three address bytes are staged in
; scratch of their own. gs0 to gs2, not t2 to t4: gsinit is walking t3.
ghsgot: LD A <- [D1]
        LD [gs0] <- A
        LD A <- [D1+1]
        LD [gs1] <- A
        LD A <- [D1+2]
        LD [gs2] <- A
        LD A <- [t1]
        ADD A <- 1           ; ghost 1 is sprite 2
        OUTA GPU_SPRITE
        LD A <- [gs0]
        OUTA GPU_SRC_BANK
        LD A <- [gs1]
        OUTA GPU_SRC_HI
        LD A <- [gs2]
        OUTA GPU_SRC_LO
        OUT GPU_CMD, CMD_SPRITE_DEF
        RET

; --- the level's three speeds, out of the table row lvlptr stands on. Pac-Man's
; goes straight into his record, because he has no ghcom to hand it to; the
; ghosts' waits in ghspd for the four calls that follow.
; DESIGN: the record, not the window, for the same reason placeghosts writes
; records. drawmaze calls this before any actor has been loaded into the
; window at all, and a per-level value belongs in an actor's durable home.
lvlspd: LD D1 <- [lvlptr]
        LD A <- [D1]
        LD [pacspd] <- A
        LD A <- [D1+1]
        LD [ghspd] <- A
        LD A <- [D1+6]
        LD [ghtun] <- A
        RET

; --- one level on, six bytes on, and never past the last row.
; DESIGN: a pointer that advances, never an index. The CPU has no multiply, so
; level times six would be a walk of its own every time the row was read.
; lvlrem counts the advances still owed before the last row: it starts at 20,
; one short of the 21 rows, and an advance spends one. At zero the pointer
; holds where it is, which is the last row, and level 22 and every level after
; it reads that same row. A pointer that kept advancing would be six bytes
; past the table on level 22, reading whatever RAM follows it.
lvlstep: LD A <- [lvlrem]
        JZ lvlsdone
        SUB A <- 1
        LD [lvlrem] <- A
        LD D1 <- [lvlptr]
        LD D1 <- D1+7
        LD [lvlptr] <- D1
lvlsdone: RET

; count one dot or pill toward the level's total. See the DESIGN note at
; the call sites in dmdot and dmpill for why this is D2, not D1.
bump:   LD D2 <- dots
        LD A <- [D2+1]
        ADD A <- 1
        LD [D2+1] <- A
        JC bumphi
        RET
bumphi: LD A <- [D2]
        ADD A <- 1
        LD [D2] <- A
        RET

; a full 8 by 8 cell at (px, py)
cellrect: LD A <- [px]
        LD [rx0] <- A
        ADD A <- 7
        LD [rx1] <- A
        LD A <- [py]
        LD [ry0] <- A
        ADD A <- 7
        LD [ry1] <- A
        JMP rectxy            ; tail call: rectxy's RET answers cellrect's caller

; draw a filled rectangle from (rx0, ry0) to (rx1, ry1). Factored out of
; cellrect so wallpaint below, which draws up to four rectangles per wall
; tile, does not repeat this port sequence four more times. A rectangle
; with rx0 = rx1 or ry0 = ry1 is a one-pixel-wide line spanning the other
; axis, which is exactly what the wall outlines need: see CMD_RECT in
; gpu.ts, which fills min..max on each axis independently.
; The corners are maze pixels and the GPU wants screen pixels, so mzxleft and
; mzytop are added on the way out. rx1 is at most 223 and ry1 at most 247, so
; both sums stay in a byte.
rectxy: OUT GPU_X_HI, 0
        OUT GPU_Y_HI, 0
        LD A <- [rx0]
        ADD A <- [mzxleft]
        OUTA GPU_X
        LD A <- [ry0]
        ADD A <- [mzytop]
        OUTA GPU_Y
        OUT GPU_CMD, CMD_MOVE_TO
        LD A <- [rx1]
        ADD A <- [mzxleft]
        OUTA GPU_X
        LD A <- [ry1]
        ADD A <- [mzytop]
        OUTA GPU_Y
        OUT GPU_CMD, CMD_RECT
        RET

; --- draw the exposed sides and the corners of the wall tile at
; (dcol, drow) / (px, py). All eight neighbours decide it, not four.
;
; An orthogonal side facing open ground draws a line inset two pixels from
; the edge it faces: top at py+2, bottom at py+5, left at px+2, right at
; px+5. A side whose flanking neighbour is wall keeps its full 8 pixel
; span, so straight runs still join across tiles. An end that meets
; another exposed side stops four pixels short, which is what leaves room
; for the round. A wall one tile thick has open ground on both faces, so
; it draws both lines and comes out four pixels from line to line.
; That leaves exactly twelve clear pixels across a one-tile corridor, which
; is what the first, 12 pixel actors were sized to. Corridor tile c has a
; wall at c-1 whose right line lands at 8(c-1)+5, and a wall at c+1 whose
; left line lands at 8(c+1)+2, so the open span runs 8c-2 to 8c+9.
; DESIGN: the actors are 14 pixels now. actdraw centres one at 8c-3, so a
; tile-aligned actor laps one pixel onto each wall line. Those pixels are
; the sprite's anti-aliased rim, dark shades of the body, so over the line
; they read as the edge of the round and not as an overlap. Between tiles
; he laps further, which costs nothing: the
; GPU composites sprites over the framebuffer every frame and never writes
; them into video memory, so nothing has to be restored behind him.
;
; Where two exposed sides meet, the outer corner is rounded. The two lines
; stop short and one diagonal pixel stands between their ends, and the
; exact corner pixel is never drawn. That omission is the rounding.
;
; Where a DIAGONAL neighbour is open while both flanking orthogonals are
; wall, the corridor turns around this tile's corner, and the inner corner
; is drawn in that quadrant. It joins two lines the NEIGHBOURING tiles
; drew, not this one's. Worked example, CLASSIC's tile (0, 0), wall below
; and wall right, corridor at (1, 1): the right neighbour draws a bottom
; line at y 5 from x 8, the lower neighbour a right line at x 5 from y 8,
; and this tile's bottom-right quadrant joins (8, 5) to (5, 8). Same shape
; as the outer round, two line ends with one diagonal step between them
; and the exact corner pixel left dark. Under the four-neighbour rule this
; tile drew nothing at all, and every junction in every maze leaked there.
;
; A tile with wall on all eight sides draws nothing, so a thick block
; still comes out hollow. See openat above for the out-of-grid and
; marker-tile rules that decide "not a wall" for each neighbour.
;
; DESIGN: a single pixel is drawn as a rectangle whose two corners are
; equal, through the same rectxy the lines use. rectxy fills min..max on
; each axis independently (see CMD_RECT in gpu.ts), so rx0 = rx1 and
; ry0 = ry1 is one pixel, and no second helper is needed for the rounds.
;
; DESIGN: the eight neighbours are read once each into nup, ndn, nlf, nrt
; and the four diagonals, then consulted from there. The horizontal span
; alone reads nlf twice and nrt twice, once to decide whether the span is
; empty and once to place that end, and calling openat again each time
; would repeat a rowtab lookup for an answer already in hand.
;
; DESIGN: the reads are ordered by ROW, middle first, so t2 is written
; three times rather than once per neighbour: the left and right reads
; share row drow, and each diagonal shares its row with the orthogonal
; above or below it. openat reads t1 and t2 and never writes either, so a
; second call in the same row only has to move t1.
;
; DESIGN: a diagonal is read only where it can matter. An inner corner
; needs BOTH flanking orthogonals to be wall, so a quadrant whose vertical
; or horizontal neighbour is already open can never hold one, and its
; diagonal is never asked for. Reading all four unconditionally would be
; four more openat calls on every wall tile; counted over the four shipped
; mazes, the guard turns 2128, 2064, 1600 and 1640 diagonal reads into
; 810, 710, 494 and 462. The draw budget is the binding constraint here,
; so the guard is what pays for the extra geometry.
;
; DESIGN: openat guards the row and the column separately, row against 31
; and column against 28, each by an unsigned SUB whose borrow the JC
; tests. A diagonal is two offsets rather than one and needs no guard of
; its own: its row is exactly the row guard of the orthogonal above or
; below it, and its column exactly the column guard of the one left or
; right. Traced, not assumed. drow 0 minus 1 gives t2 255, and 255 minus
; 31 does not borrow, so the JC misses and the tile reads open; dcol 27
; plus 1 gives t1 28, and 28 minus 28 does not borrow either.
;
; DESIGN: reads dcol and drow rather than recomputing a tile position from
; px and py. drawmaze's walk already keeps dcol/drow current for tileat
; and tileptr elsewhere in this same loop, so wallpaint arrives here with
; both already set to the tile dmwall just dispatched on.
;
; DESIGN: PUSH D1 brackets the whole body against a single POP D1 at
; wpdone, which is the only RET. Every path through the eight reads, the
; four lines and the four quadrants reaches wpdone by falling through or
; by a JMP, so the push and the pop stay one to one however the branches
; go. D1 is drawmaze's own walking pointer through work, live for the
; whole draw, and openat uses D1 as scratch for its rowtab lookup on any
; of the up to eight neighbours checked here that lands in range (an
; out-of-range neighbour returns at oaopen without touching D1, but a wall
; tile can still have any mix of in-range and out-of-range neighbours from
; one call to the next, so the save has to cover the whole body).
; tileptr and blank get away with clobbering D1 because they recompute
; rowtab[drow]+dcol, bit for bit the walking pointer's current value (see
; the DESIGN comment above dmdot); openat looks up a NEIGHBOUR's row, a
; different address, so nothing recomputes the walk position afterward.
; Saving and restoring it here is what lets dmnext's INC D1 keep advancing
; the same tile the rest of the draw expects.
wallpaint:
        PUSH D1
        LD A <- 0            ; a diagonal not read below stays closed
        LD [nul] <- A
        LD [nur] <- A
        LD [ndl] <- A
        LD [ndr] <- A
        LD A <- [drow]       ; this tile's own row: left, then right
        LD [t2] <- A
        LD A <- [dcol]
        SUB A <- 1
        LD [t1] <- A
        JSR openat
        LD [nlf] <- A
        LD A <- [dcol]
        ADD A <- 1
        LD [t1] <- A
        JSR openat
        LD [nrt] <- A
        LD A <- [drow]       ; the row above: up, then its two diagonals
        SUB A <- 1
        LD [t2] <- A
        LD A <- [dcol]
        LD [t1] <- A
        JSR openat
        LD [nup] <- A
        JNZ wprowd             ; up is wall, so an inner corner is possible
wpdgu:  LD A <- [nlf]
        JNZ wpdgu2
wpdgul: LD A <- [dcol]
        SUB A <- 1
        LD [t1] <- A
        JSR openat
        LD [nul] <- A
wpdgu2: LD A <- [nrt]
        JNZ wprowd
wpdgur: LD A <- [dcol]
        ADD A <- 1
        LD [t1] <- A
        JSR openat
        LD [nur] <- A
wprowd: LD A <- [drow]       ; the row below: down, then its two diagonals
        ADD A <- 1
        LD [t2] <- A
        LD A <- [dcol]
        LD [t1] <- A
        JSR openat
        LD [ndn] <- A
        JNZ wphor
wpdgd:  LD A <- [nlf]
        JNZ wpdgd2
wpdgdl: LD A <- [dcol]
        SUB A <- 1
        LD [t1] <- A
        JSR openat
        LD [ndl] <- A
wpdgd2: LD A <- [nrt]
        JNZ wphor
wpdgdr: LD A <- [dcol]
        ADD A <- 1
        LD [t1] <- A
        JSR openat
        LD [ndr] <- A

; the top and bottom lines. Both take the same x span, so it is computed
; once: the left end at px+4 where the left side is exposed too and px
; where it is not, the right end at px+3 or px+7 by the same rule.
wphor:  LD A <- [nup]
        JNZ wphspn
wphor2: LD A <- [ndn]
        JZ wpver             ; neither face is open: no horizontal line
; left and right both exposed leaves the span empty, px+4 to px+3. Draw no
; line at all then and let the two rounds below cap the tile, rather than
; letting rectxy normalise the inverted span into a two pixel stub.
wphspn: LD A <- [nlf]
        JZ wphs1
        LD A <- [nrt]
        JNZ wpver
wphs1:  LD A <- [nlf]
        JZ wphs2
        LD A <- 4
        JMP wphs3
wphs2:  LD A <- 0
wphs3:  ADD A <- [px]
        LD [rx0] <- A
        LD A <- [nrt]
        JZ wphs4
        LD A <- 3
        JMP wphs5
wphs4:  LD A <- 7
wphs5:  ADD A <- [px]
        LD [rx1] <- A
        LD A <- [nup]
        JZ wphbot
        LD A <- [py]
        ADD A <- 2
        LD [ry0] <- A
        LD [ry1] <- A
        JSR rectxy
wphbot: LD A <- [ndn]
        JZ wpver
        LD A <- [py]
        ADD A <- 5
        LD [ry0] <- A
        LD [ry1] <- A
        JSR rectxy

; the left and right lines, the same shape turned ninety degrees: one y
; span shared by both, py+4 or py where the top is exposed or not, py+3 or
; py+7 at the other end.
wpver:  LD A <- [nlf]
        JNZ wpvspn
wpver2: LD A <- [nrt]
        JZ wpcorn
wpvspn: LD A <- [nup]
        JZ wpvs1
        LD A <- [ndn]
        JNZ wpcorn
wpvs1:  LD A <- [nup]
        JZ wpvs2
        LD A <- 4
        JMP wpvs3
wpvs2:  LD A <- 0
wpvs3:  ADD A <- [py]
        LD [ry0] <- A
        LD A <- [ndn]
        JZ wpvs4
        LD A <- 3
        JMP wpvs5
wpvs4:  LD A <- 7
wpvs5:  ADD A <- [py]
        LD [ry1] <- A
        LD A <- [nlf]
        JZ wpvrgt
        LD A <- [px]
        ADD A <- 2
        LD [rx0] <- A
        LD [rx1] <- A
        JSR rectxy
wpvrgt: LD A <- [nrt]
        JZ wpcorn
        LD A <- [px]
        ADD A <- 5
        LD [rx0] <- A
        LD [rx1] <- A
        JSR rectxy

; the four quadrants. An inner corner and an outer round can never both
; land in one quadrant: a diagonal flag is only ever set where both
; flanking orthogonals are wall, and the round only fires where both are
; open, so the two tests are mutually exclusive by construction.
; top left
wpcorn: LD A <- [nul]
        JZ wpctl2
        LD A <- [px]
        LD [rx0] <- A
        ADD A <- 1
        LD [rx1] <- A
        LD A <- [py]
        ADD A <- 2
        LD [ry0] <- A
        LD [ry1] <- A
        JSR rectxy
        LD A <- [px]
        ADD A <- 2
        LD [rx0] <- A
        LD [rx1] <- A
        LD A <- [py]
        LD [ry0] <- A
        ADD A <- 1
        LD [ry1] <- A
        JSR rectxy
        LD A <- 2
        LD [wdx] <- A
        LD A <- 2
        LD [wdy] <- A
        JSR wdim
        JMP wpctr
wpctl2: LD A <- [nup]
        JZ wpctr
        LD A <- [nlf]
        JZ wpctr
        LD A <- [px]
        ADD A <- 3
        LD [rx0] <- A
        LD [rx1] <- A
        LD A <- [py]
        ADD A <- 3
        LD [ry0] <- A
        LD [ry1] <- A
        JSR rectxy
; top right
        LD A <- 3
        LD [wdx] <- A
        LD A <- 2
        LD [wdy] <- A
        JSR wdim
        LD A <- 2
        LD [wdx] <- A
        LD A <- 3
        LD [wdy] <- A
        JSR wdim
wpctr:  LD A <- [nur]
        JZ wpctr2
        LD A <- [px]
        ADD A <- 6
        LD [rx0] <- A
        ADD A <- 1
        LD [rx1] <- A
        LD A <- [py]
        ADD A <- 2
        LD [ry0] <- A
        LD [ry1] <- A
        JSR rectxy
        LD A <- [px]
        ADD A <- 5
        LD [rx0] <- A
        LD [rx1] <- A
        LD A <- [py]
        LD [ry0] <- A
        ADD A <- 1
        LD [ry1] <- A
        JSR rectxy
        LD A <- 5
        LD [wdx] <- A
        LD A <- 2
        LD [wdy] <- A
        JSR wdim
        JMP wpcbl
wpctr2: LD A <- [nup]
        JZ wpcbl
        LD A <- [nrt]
        JZ wpcbl
        LD A <- [px]
        ADD A <- 4
        LD [rx0] <- A
        LD [rx1] <- A
        LD A <- [py]
        ADD A <- 3
        LD [ry0] <- A
        LD [ry1] <- A
        JSR rectxy
; bottom left
        LD A <- 4
        LD [wdx] <- A
        LD A <- 2
        LD [wdy] <- A
        JSR wdim
        LD A <- 5
        LD [wdx] <- A
        LD A <- 3
        LD [wdy] <- A
        JSR wdim
wpcbl:  LD A <- [ndl]
        JZ wpcbl2
        LD A <- [px]
        LD [rx0] <- A
        ADD A <- 1
        LD [rx1] <- A
        LD A <- [py]
        ADD A <- 5
        LD [ry0] <- A
        LD [ry1] <- A
        JSR rectxy
        LD A <- [px]
        ADD A <- 2
        LD [rx0] <- A
        LD [rx1] <- A
        LD A <- [py]
        ADD A <- 6
        LD [ry0] <- A
        ADD A <- 1
        LD [ry1] <- A
        JSR rectxy
        LD A <- 2
        LD [wdx] <- A
        LD A <- 5
        LD [wdy] <- A
        JSR wdim
        JMP wpcbr
wpcbl2: LD A <- [ndn]
        JZ wpcbr
        LD A <- [nlf]
        JZ wpcbr
        LD A <- [px]
        ADD A <- 3
        LD [rx0] <- A
        LD [rx1] <- A
        LD A <- [py]
        ADD A <- 4
        LD [ry0] <- A
        LD [ry1] <- A
        JSR rectxy
; bottom right
        LD A <- 2
        LD [wdx] <- A
        LD A <- 4
        LD [wdy] <- A
        JSR wdim
        LD A <- 3
        LD [wdx] <- A
        LD A <- 5
        LD [wdy] <- A
        JSR wdim
wpcbr:  LD A <- [ndr]
        JZ wpcbr2
        LD A <- [px]
        ADD A <- 6
        LD [rx0] <- A
        ADD A <- 1
        LD [rx1] <- A
        LD A <- [py]
        ADD A <- 5
        LD [ry0] <- A
        LD [ry1] <- A
        JSR rectxy
        LD A <- [px]
        ADD A <- 5
        LD [rx0] <- A
        LD [rx1] <- A
        LD A <- [py]
        ADD A <- 6
        LD [ry0] <- A
        ADD A <- 1
        LD [ry1] <- A
        JSR rectxy
        LD A <- 5
        LD [wdx] <- A
        LD A <- 5
        LD [wdy] <- A
        JSR wdim
        JMP wpdone
wpcbr2: LD A <- [ndn]
        JZ wpdone
        LD A <- [nrt]
        JZ wpdone
        LD A <- [px]
        ADD A <- 4
        LD [rx0] <- A
        LD [rx1] <- A
        LD A <- [py]
        ADD A <- 4
        LD [ry0] <- A
        LD [ry1] <- A
        JSR rectxy
        LD A <- 5
        LD [wdx] <- A
        LD A <- 4
        LD [wdy] <- A
        JSR wdim
        LD A <- 4
        LD [wdx] <- A
        LD A <- 5
        LD [wdy] <- A
        JSR wdim
wpdone: POP D1
        RET

; --- one wall pixel at (px + wdx, py + wdy) in the wall's half shade, then
; the wall colour back. The rounds call it: an outer round gets the two
; pixels outside its diagonal step, an inner corner the pixel it used to
; leave dark, so every curve of the maze is anti-aliased. Straight runs lie
; on the pixel grid and need nothing.
; DESIGN: the half shade is a palette entry of its own, WALLDIM, that the
; fades write beside the wall's, so the rim fades in and out with the wall.
wdim:   LD A <- [px]
        ADD A <- [wdx]
        LD [rx0] <- A
        LD [rx1] <- A
        LD A <- [py]
        ADD A <- [wdy]
        LD [ry0] <- A
        LD [ry1] <- A
        OUT GPU_COLOR, WALLDIM
        OUT GPU_CMD, CMD_SET_COLOR
        JSR rectxy
        OUT GPU_COLOR, 1
        OUT GPU_CMD, CMD_SET_COLOR
        RET

; --- a dot in the middle of the cell at (px, py): a 2 by 2 core in the
; colour the caller set, entry 2, and a rim of half shade on its four sides.
; The rim is what a round dot of that size covers in part, so it reads as
; round and anti-aliased. The cell is 8 by 8 and the dot stays inside it, so
; cellrect still paints it all out when he eats it.
dotrect:        LD A <- 3
        LD [cx0] <- A
        LD A <- 3
        LD [cy0] <- A
        LD A <- 4
        LD [cx1] <- A
        LD A <- 4
        LD [cy1] <- A
        JSR cellpart
        OUT GPU_COLOR, DOTDIM
        OUT GPU_CMD, CMD_SET_COLOR
        LD A <- 3
        LD [cx0] <- A
        LD A <- 2
        LD [cy0] <- A
        LD A <- 4
        LD [cx1] <- A
        LD A <- 2
        LD [cy1] <- A
        JSR cellpart
        LD A <- 3
        LD [cx0] <- A
        LD A <- 5
        LD [cy0] <- A
        LD A <- 4
        LD [cx1] <- A
        LD A <- 5
        LD [cy1] <- A
        JSR cellpart
        LD A <- 2
        LD [cx0] <- A
        LD A <- 3
        LD [cy0] <- A
        LD A <- 2
        LD [cx1] <- A
        LD A <- 4
        LD [cy1] <- A
        JSR cellpart
        LD A <- 5
        LD [cx0] <- A
        LD A <- 3
        LD [cy0] <- A
        LD A <- 5
        LD [cx1] <- A
        LD A <- 4
        LD [cy1] <- A
        JSR cellpart
        OUT GPU_COLOR, 2
        OUT GPU_CMD, CMD_SET_COLOR
        RET

; --- a pill: a 4 by 4 core in entry 3, its corners and a rim around its
; sides in the half shade, a disc of about five pixels anti-aliased.
pillrect:        LD A <- 2
        LD [cx0] <- A
        LD A <- 2
        LD [cy0] <- A
        LD A <- 5
        LD [cx1] <- A
        LD A <- 5
        LD [cy1] <- A
        JSR cellpart
        OUT GPU_COLOR, DOTDIM
        OUT GPU_CMD, CMD_SET_COLOR
        LD A <- 2
        LD [cx0] <- A
        LD A <- 2
        LD [cy0] <- A
        LD A <- 2
        LD [cx1] <- A
        LD A <- 2
        LD [cy1] <- A
        JSR cellpart
        LD A <- 5
        LD [cx0] <- A
        LD A <- 2
        LD [cy0] <- A
        LD A <- 5
        LD [cx1] <- A
        LD A <- 2
        LD [cy1] <- A
        JSR cellpart
        LD A <- 2
        LD [cx0] <- A
        LD A <- 5
        LD [cy0] <- A
        LD A <- 2
        LD [cx1] <- A
        LD A <- 5
        LD [cy1] <- A
        JSR cellpart
        LD A <- 5
        LD [cx0] <- A
        LD A <- 5
        LD [cy0] <- A
        LD A <- 5
        LD [cx1] <- A
        LD A <- 5
        LD [cy1] <- A
        JSR cellpart
        LD A <- 3
        LD [cx0] <- A
        LD A <- 1
        LD [cy0] <- A
        LD A <- 4
        LD [cx1] <- A
        LD A <- 1
        LD [cy1] <- A
        JSR cellpart
        LD A <- 3
        LD [cx0] <- A
        LD A <- 6
        LD [cy0] <- A
        LD A <- 4
        LD [cx1] <- A
        LD A <- 6
        LD [cy1] <- A
        JSR cellpart
        LD A <- 1
        LD [cx0] <- A
        LD A <- 3
        LD [cy0] <- A
        LD A <- 1
        LD [cx1] <- A
        LD A <- 4
        LD [cy1] <- A
        JSR cellpart
        LD A <- 6
        LD [cx0] <- A
        LD A <- 3
        LD [cy0] <- A
        LD A <- 6
        LD [cx1] <- A
        LD A <- 4
        LD [cy1] <- A
        JSR cellpart
        OUT GPU_COLOR, 3
        OUT GPU_CMD, CMD_SET_COLOR
        RET

; --- the rectangle from (px + cx0, py + cy0) to (px + cx1, py + cy1).
cellpart: LD A <- [px]
        ADD A <- [cx0]
        LD [rx0] <- A
        LD A <- [px]
        ADD A <- [cx1]
        LD [rx1] <- A
        LD A <- [py]
        ADD A <- [cy0]
        LD [ry0] <- A
        LD A <- [py]
        ADD A <- [cy1]
        LD [ry1] <- A
        JMP rectxy

; --- fade the three maze entries up over 30 frames.
; DESIGN: no multiply, so each frame adds a fixed step and the last frame
; writes the exact target. Four counters, not three: the wall entry fades
; blue alone, but the dot and pill target (255, 224, 176) needs red, green
; and blue all moving, and a shared blue counter cannot ramp to 255 for the
; wall and to 176 for the dot in the same run.
fadein: LD A <- 0
        LD [fstep] <- A
        LD [fwb] <- A
        LD [fr] <- A
        LD [fg] <- A
        LD [fdb] <- A
fiwait: IN GPU_FRAME
        CMP A, [fframe]
        JZ fiwait
        LD [fframe] <- A          ; CMP kept the frame in A
        LD A <- [fwb]         ; wall blue climbs by 8
        ADD A <- 8
        LD [fwb] <- A
        LD A <- [fr]          ; dot and pill red by 8, green by 7, blue by 5
        ADD A <- 8
        LD [fr] <- A
        LD A <- [fg]
        ADD A <- 7
        LD [fg] <- A
        LD A <- [fdb]
        ADD A <- 5
        LD [fdb] <- A
        JSR fput
        LD A <- [fstep]
        INC A
        LD [fstep] <- A
        SUB A <- 30
        JNZ fiwait
; The exact colours, so the end of the fade is not 240.
fisnap: LD D1 <- palbuf+3
        LD A <- 0
        LD [D1]+ <- A
        LD [D1]+ <- A
        LD A <- 255
        LD [D1]+ <- A
        LD [D1]+ <- A
        LD A <- 224
        LD [D1]+ <- A
        LD A <- 176
        LD [D1]+ <- A
        LD A <- 255
        LD [D1]+ <- A
        LD A <- 224
        LD [D1]+ <- A
        LD A <- 176
        LD [D1] <- A
        LD D1 <- palbuf+WALLDIM*3
        LD A <- 0
        LD [D1]+ <- A
        LD [D1]+ <- A
        LD A <- 128
        LD [D1]+ <- A
        LD [D1]+ <- A
        LD A <- 112
        LD [D1]+ <- A
        LD A <- 88
        LD [D1] <- A
        OUT GPU_CMD, CMD_FETCH_PALETTE
        RET

; Write the three maze entries from the four counters, then install them.
; Nine bytes through a pointer, where it used to be nine port writes.
fput:   LD D1 <- palbuf+3
        LD A <- 0            ; the wall is blue only
        LD [D1]+ <- A
        LD [D1]+ <- A
        LD A <- [fwb]
        LD [D1]+ <- A
        LD A <- [fr]         ; dots
        LD [D1]+ <- A
        LD A <- [fg]
        LD [D1]+ <- A
        LD A <- [fdb]
        LD [D1]+ <- A
        LD A <- [fr]         ; pills, the same colour as dots
        LD [D1]+ <- A
        LD A <- [fg]
        LD [D1]+ <- A
        LD A <- [fdb]
        LD [D1] <- A
; the half shades, entries 5 and 6: each counter halved
        LD D1 <- palbuf+WALLDIM*3
        LD A <- 0
        LD [D1]+ <- A
        LD [D1]+ <- A
        LD A <- [fwb]
        SHR A
        LD [D1]+ <- A
        LD A <- [fr]
        SHR A
        LD [D1]+ <- A
        LD A <- [fg]
        SHR A
        LD [D1]+ <- A
        LD A <- [fdb]
        SHR A
        LD [D1] <- A
        OUT GPU_CMD, CMD_FETCH_PALETTE
        RET

; =========================== actor movement ===========================
; --- copy the record D1 points at into the window, and remember where it came
; from so saveact can put it back.
; DESIGN: D1 is the argument, and neither routine hands it back changed on
; purpose. loadact reads through it and leaves it exactly as the caller passed
; it; saveact ignores what it was given and loads it from acur, so it comes
; back holding the record just written. Neither is a safe thing to walk with
; regardless, because what runs between them is not: actstep clobbers D1 for
; its table lookups. loadact is called from the actor walk, once per actor,
; and again from nextlvl to hand the window back; saveact only from the walk,
; which re-reads acur rather than relying on either. Checked in both bodies
; rather than assumed, openat destroying drawmaze's walking pointer being the
; standing lesson here.
loadact: LD [acur] <- D1
        LD A <- [D1]
        LD [ax] <- A
        LD A <- [D1+1]
        LD [ay] <- A
        LD A <- [D1+2]
        LD [adir] <- A
        LD A <- [D1+3]
        LD [aacc] <- A
        LD A <- [D1+4]
        LD [aspd] <- A
        LD A <- [D1+5]
        LD [astate] <- A
        LD A <- [D1+6]
        LD [ascol] <- A
        LD A <- [D1+7]
        LD [asrow] <- A
        RET

; --- put the window back into the record it came from.
; DESIGN: six fields, not eight. Fields 6 and 7 are the scatter corner, part
; of an actor's placement rather than of its step: nothing a step runs ever
; writes them, so the byte written back would always be the byte loadact just
; read out. Skipping them saves the work and costs nothing observable, which
; is also why no test pins it: a saveact that wrote all eight would produce a
; byte-identical RAM.
saveact: LD D1 <- [acur]
        LD A <- [ax]
        LD [D1] <- A
        LD A <- [ay]
        LD [D1+1] <- A
        LD A <- [adir]
        LD [D1+2] <- A
        LD A <- [aacc]
        LD [D1+3] <- A
        LD A <- [aspd]
        LD [D1+4] <- A
        LD A <- [astate]
        LD [D1+5] <- A
        RET

; --- walk all five actor records: each one into the window, worked on, put
; back, drawn. acti says which record is in the window, 0 for Pac-Man and 1 to
; 4 for Blinky, Pinky, Inky and Clyde, so the decision hook and the draw can
; tell one from the other.
; DESIGN: two entry points, one walk. actall steps and draws; drawall only
; draws, for the two moments the sprites have to follow the records with no
; step happening -- startup, and the new maze inside nextlvl, where a step is
; already in progress and must not be run again. aastep is which of the two
; this walk is, and it DOES outlive the call that writes it: nextlvl reaches
; drawall from inside a running actall, so the nested walk's 0 is still
; standing when the interrupted walk resumes, and every actor after the
; interrupted one would then be drawn without being stepped or saved. acti and
; acur are re-entered the same way. All three are saved and handed back by
; nextlvl, which is the only caller that re-enters, and none of them is safe
; to reason about as "written and read in one call".
; DESIGN: the record pointer advances by eight INC D1, because there is no add
; for a D register. That is 40 instructions a frame across the five actors,
; against a frame measured at about 16,400. A table of five record addresses
; would have to be read through D1 as well, one word per actor, which costs
; more per step than the increments it would save.
actall: LD A <- 1
        LD [aastep] <- A
; --- the house divider, stepped here and nowhere else. A release countdown
; moves one frame in eight, and the phase is one counter for the whole walk
; rather than one per ghost, so all three in the house count the same eight
; frames. It belongs on this entry and not on drawall's: a draw-only walk is
; the level change redrawing five sprites, and ageing a timer there would
; make a level's release schedule depend on how many times it was redrawn.
        LD A <- [houphs]
        INC A
        AND A <- 7
        LD [houphs] <- A
        JMP aawalk
drawall: LD A <- 0
        LD [aastep] <- A
aawalk: LD A <- 0
        LD [acti] <- A
        LD D1 <- actors
aaloop: JSR loadact
        LD A <- [aastep]
        JZ aadraw
        JSR actstep
        JSR saveact
aadraw: JSR actdraw
        LD A <- [acti]
        INC A
        LD [acti] <- A
; DESIGN: exact equality, not "five or more", because the machine has no
; unsigned compare and SUB plus JZ is the whole test. The cost of that is that
; any path re-entering here with acti above 5 runs 256 passes over whatever
; RAM follows the records rather than stopping, which is exactly what a
; nextlvl that failed to hand acti back would do (see the note there). Anything
; that adds a second re-entrant path through actstep owes the same hand-back.
        SUB A <- 5
        JZ aadone
; the record this pass loaded, plus eight, is the next one. acur is read back
; rather than trusting whatever D1 holds by now. Of the four routines called
; above, actstep is the one that clobbers it, through canmove and advance and
; their dxtab and dytab lookups; loadact takes it as its argument and leaves
; it alone, saveact reloads it from acur itself, and neither actdraw nor the
; setframe it calls mentions D1 at all (counted: zero occurrences in either). So on the stepping path saveact happens to leave D1 already correct
; and on the draw-only path nothing moved it, which makes this reload
; defensive rather than load-bearing today. It stays because the walk must not
; depend on the internals of four routines staying that way (openat destroying
; drawmaze's walking pointer is the standing lesson).
        LD D1 <- [acur]
        LD D1 <- D1+8
        JMP aaloop
; --- the pass is over, so the reversal it was carrying is spent.
; DESIGN: cleared here, at the end of the walk, and not by a counter of the
; ghosts that took it. The walk offers every one of the five actors exactly
; one step per frame, so by the time this line is reached all four ghosts have
; been asked; one still in the house was asked, declined at actstep's state
; test, and held nothing up. A counter would say the same thing and cost a
; byte and a reset of its own.
; The draw-only entry is excluded, and that guard is defensive rather than
; load-bearing: the one draw-only walk that runs mid-game is the one nextlvl
; makes, and drawmaze, which zeroes the flag, is the call immediately before
; it. Deleting the guard leaves all 89 tests green. It stays because a draw
; has stepped nobody, so it has offered the flag to nobody, and a second
; re-entrant draw that did not happen to follow a drawmaze would spend a mode
; change on a redraw. See the aastep note at actall for why re-entrancy here
; is not hypothetical.
aadone: LD A <- [aastep]
        JZ aaend
        LD A <- 0
        LD [modefl] <- A
aaend:  RET

; --- one step of whichever actor is in the window. An actor holds pixel
; coordinates and a speed byte. Each frame it adds the speed to an
; accumulator and moves one pixel when that carries. 205 is 80 percent of a
; pixel a frame. Direction: 0 right, 1 down, 2 left, 3 up. A reversal is the
; direction exclusive-or 2.
; DESIGN: state 0 means "in the house", and the whole of the house is off the
; maze rules: a ghost in there is on the scripted walk below, waiting or
; leaving, and neither targets nor chooses. The test is on the state byte
; alone and not on acti as well, because state 0 IS the house and no actor is
; ever in it by accident. Pac-Man's record ships with state 1 and nothing ever
; changes it. saveact does write field 5 back for every actor once a frame, his
; included, but it writes back exactly what loadact read, and the only two
; things that alter the byte in between are placeghosts, which writes the four
; ghost records and not his, and ashfree, which is on the far side of the branch
; this test guards. So he never reaches it, and a later task that gives him a
; state of his own owes it a value that is not 0.
actstep: LD A <- [astate]
        JZ ashwait
        CMP A, 3
        JZ aseyes            ; eaten: on the way home, and on its own rules
        JSR modeturn
        JMP asacc
; --- eyes, on their way back to the house. They are asked whether they have
; arrived and then stepped like anything else.
; DESIGN: modeturn is not offered to them, which is the arcade's rule and this
; branch's own shape rather than a test bolted on. A forced reversal is a
; hunt's business and eyes are not hunting, and a reversal here is simply a
; step in the wrong direction: eyedir reads the distance field, so it already
; knows which of the four ways home is, and modeturn would spend a tile
; walking away from it before the next centre put it right.
; DESIGN: arriving IS this tick's move, so a ghost that got home does not also
; take a pixel. That is what makes the arrival readable: the record stands on
; the tile the field is seeded at rather than one pixel past it. astate is what
; says which happened, because eyehome writes 0 into it exactly when it
; revives.
aseyes: JSR eyehome
        LD A <- [astate]
        JZ asstop
        JMP asacc
; --- waiting in the house. The countdown is in frames, through the divider
; actall steps, so it is counted BEFORE the speed accumulator: a slow ghost
; must not also be a late one, and task 9's per-level speeds would do exactly
; that if the wait were counted in movement ticks.
ashwait: JSR houptr           ; D1 := this actor's countdown byte
        LD A <- [D1]
        JZ asacc             ; the wait is over: walk out at the ghost's speed
        LD A <- [houphs]
        JZ ashtick           ; one frame in eight steps the countdown
        RET
ashtick: LD A <- [D1]
        SUB A <- 1
        LD [D1] <- A
        RET
; --- one tick of the speed accumulator, and the only place a speed becomes a
; pixel. What gets added is not always the record's own byte.
; DESIGN: a modifier, never a write to aspd. saveact puts the window's aspd
; back into the record, so a tunnel that lowered aspd would follow the ghost
; out of the tunnel and hold it there for the rest of the level. The record
; keeps the speed the level gave it; this picks what THIS tick adds to the
; accumulator and leaves the record alone.
asacc:  LD A <- [acti]
        JZ asapac            ; Pac-Man: the eating pause, and no tunnel
; DESIGN: the state test comes before intunnel, not after it. Only a ghost out
; on the maze or frightened is slowed: eyes go home at full speed, tunnel or
; not, and a ghost in the house is not in a corridor at all. Testing the state
; first also keeps intunnel off the two paths that would never use its answer,
; which is what makes the whole modifier cost a couple of hundred instructions
; a frame against a frame measured at about 16,400.
        LD A <- [astate]
        JZ asanorm           ; 0: in the house
        CMP A, 3
        JZ asanorm           ; 3: eyes, and they are not slowed
        JSR intunnel
        JZ asanorm
        LD A <- [ghtun]
        JMP asadd
; --- Pac-Man pauses outright for a frame on a dot and three on a pill, which
; is the arcade's way of making a mouthful cost something. Counted here and in
; whole frames, so he loses the tick rather than a fraction of a pixel.
asapac: LD A <- [pacstl]
        JZ asanorm
        SUB A <- 1
        LD [pacstl] <- A
        RET                  ; the tick is spent standing still
asanorm: LD A <- [aspd]
asadd:  ADD A <- [aacc]
        LD [aacc] <- A
        JC asgo
        RET
; DESIGN: there is no jump-if-not-zero, so "off centre, just glide" has to
; be the fall-through and "centred, a turn or a wall may apply" has to be
; the explicit target reached through an extra label and JMP. atcentre
; returns zero exactly when centred (both AND A <- 7 tests hit zero), so
; JZ alone would land the turn and wall logic off centre and let advance
; run unconditionally on centre, which is backwards from the design: turns
; and wall tests happen only when centred (see the background note above).
; The state test in front of it is the second half of the house branch above:
; the wait is per frame and lands before the accumulator, the walk is per
; moved pixel and lands here, after it.
asgo:   LD A <- [astate]
        JZ ashwalk            ; in the house: the scripted walk out, no maze
        JSR atcentre
        JNZ asmv   ; off centre: keep going the way he is
; centred: the actor chooses a direction. Which actor is in the window decides
; who is asked. Pac-Man eats and reads the pad; a ghost must do neither.
asturn: LD A <- [acti]
        JZ asdpac
        JSR ghdecide
; and on into the same wrap, canmove and advance Pac-Man uses, which is the
; whole point of the shared step. A JMP, not a fall-through: pacdecide is the
; next thing here and a ghost must never read the pad.
        JMP assame
asdpac: JSR pacdecide
; DESIGN: OVERRIDE. wrap runs before canmove now, and it did run after. It
; asks "is the actor standing on a tunnel mouth, heading anywhere but back up
; that mouth's own corridor", which is a question about the tile it is on and
; not about the tile it is entering, so canmove has nothing to contribute to
; it. Order matters because Metro Station's second pair is a pocket with wall
; on three sides: the step into that tunnel is a step canmove refuses, so with
; canmove first the actor never reached wrap and simply stood there. See wrap.
; It runs only from here, so only from a tile centre and only after the actor
; has chosen; the off-centre path below goes straight to advance, and an
; off-centre actor is never on a mouth anyway, mouths being tile positions.
; A warp IS this tick's move: the actor comes out a whole tile past the far
; mouth, so there is no pixel left to take and nothing to ask canmove about.
assame: JSR wrap
        JZ ascm
        RET
ascm:   LD A <- [adir]
        JSR canmove
        JZ asstop
asmv:   JSR advance
; DESIGN: the step no longer draws. actall draws every actor once a frame,
; after the write-back, so a sprite follows its record whether or not the
; actor moved this tick. Leaving the draw in here would mean a ghost that
; never steps is never drawn, and an actor whose accumulator did not carry
; keeps a sprite the last draw happened to leave behind.
asstop: RET

; --- the scatter and chase clock, stepped once a frame from the main loop.
; DESIGN: after the actor walk, not before it. The flag a flip raises has to
; outlive the frame it is raised in. Raised before the walk, it would be taken
; and cleared inside that same pass, so the only samples that could ever see a
; mode change standing are the ones that land inside the walk: measured on
; this build over 3000 runToNextFrame calls, 2917 stop on one of the frame
; loop's two GPU_FRAME reads and 83 land in the walk. Raised here, it is set
; at the end of one frame and spent by the whole of the next frame's walk,
; which is the pass that offers every actor exactly one step.
; DESIGN: a duration is in units of 8 frames, so the divider is a counter
; ANDed with 7 rather than a division the CPU cannot do. Task 9 replaces the
; scatter constant msdur loads with the level table's scatter byte.
modestep: LD A <- [modetk8]
        ADD A <- 1
        LD [modetk8] <- A
        AND A <- 7
        JZ mstick
        RET
; --- an eighth frame has passed. The pill's clock counts here, and the mode's
; counts only when no pill is standing.
; DESIGN: the scatter and chase clock is HELD for the whole of a fright. That
; is the arcade's behaviour; the spec says nothing either way, so this is
; chosen rather than inherited and the note is here so the next reader knows
; which. What the alternative cost is measurable: with the clock running, one
; flip landed inside every 45 unit window sampled, and a flip forces a
; reversal, so a player saw four blue ghosts spin round with nothing on screen
; to explain it. It also handed the level back a mode phase that was wrong by
; the length of the window.
; DESIGN: held, not restarted. modetk is left exactly where it stood, so the
; mode resumes with the ticks it had left rather than with a full duration.
; The two look alike from outside -- neither flips during a fright -- which is
; why the test asserts the tick count and not just the mode.
; DESIGN: frstep runs FIRST, so the tick that closes the window is also the
; tick the mode moves on. The divider itself is never held: modetk8 is what
; both clocks count on, and holding it would stop the fright as well.
mstick: JSR frstep           ; the pill's clock counts on this same tick
        LD A <- [frtk]
        JZ msmode
        RET                  ; a pill is standing: the hunt's clock waits
msmode: LD A <- [modetk]
        SUB A <- 1
        LD [modetk] <- A
        JZ msflip
        RET
msflip: LD A <- [mode]       ; flip 0 to 1, 1 to 0
        XOR A <- 1
        LD [mode] <- A
        LD A <- 1
        LD [modefl] <- A     ; every ghost out of the house owes one reversal
        JMP msdur            ; a tail call: msdur's RET answers main

; --- load modetk with the duration of whichever mode is standing.
; DESIGN: one routine, two callers. The flip above and drawmaze's per-level
; reset both have to load a duration, and a level has to start on a full
; scatter timer rather than on whatever the last one had left.
; DESIGN: OVERRIDE. Scatter is read out of the level table now, byte 5 of the
; row lvlptr stands on, and it is no longer a constant in this source. The
; table's ramp is what asked for it: scatter shrinks from 52 units to 38 at
; level 5 while the hunt keeps its full twenty seconds, so more of each level
; goes to the chase. Chase has no byte in the table and stays a constant, from
; PACMAN_CHASE_UNITS at the top of pacman-src.ts, which is also where the
; tests read it. PACMAN_SCATTER_UNITS is still exported, and it is now level
; 1's own row rather than a second copy of the number.
msdur:  LD A <- [mode]
        JZ msdscat
        LD A <- 150
        LD [modetk] <- A
        RET
msdscat: LD D1 <- [lvlptr]
        LD A <- [D1+5]
        LD [modetk] <- A
        RET

; =========================== the power pill ===========================
; --- one tick of the fright clock, from the mode clock's own divider.
; DESIGN: the same eighth-frame tick and not a second divider. A fright
; duration is in units of 8 frames exactly as a mode duration is, and one
; counter for both is the same rule the machine keeps everywhere else: one
; unit does the job, and a second one to do it again is what drifts. The price
; is that the window is up to seven frames short of the duration times eight,
; because the first tick lands on the next eighth frame rather than on the
; frame the pill fell. Measured on level 1, whose byte is 45: 353 frames
; against the 360 the duration names.
; DESIGN: this one runs on every tick, and the mode's does not. mstick above
; holds the scatter and chase countdown for the whole of a fright and calls
; this first, so the window closes on schedule whatever the mode is doing.
; DESIGN: zero means no pill is standing, so the countdown is its own flag and
; the routine costs one load on a frame with no fright.
frstep: LD A <- [frtk]
        JZ frsno
        SUB A <- 1
        LD [frtk] <- A
        JZ unfright          ; the window shut: a tail call, its RET answers
frsno:  RET

; --- a pill was eaten: turn the hunt around.
; DESIGN: the forced reversal is the mode change's own, not a second one. A
; pill turns every ghost that is out, which is exactly what a scatter to chase
; flip does, so this raises modefl and the walk already in progress spends it:
; modeturn offers it to each actor in turn and aadone clears it at the end of
; the pass. eat runs from Pac-Man's own step and he is actor 0, so all four
; ghosts are still ahead of this in the same walk and take the turn in the
; frame the pill fell.
; DESIGN: the reversal happens even when the duration is zero. Levels 17, 19,
; 20 and 21 carry a fright duration of 0, and the spec says a pill still turns
; them round there and simply no longer turns them blue. So modefl is raised
; before the duration is looked at, and a zero duration leaves after it.
; DESIGN: state 1 only. A ghost in the house is state 0 and is not the pill's
; to touch: it is on the scripted walk out, off the maze rules entirely. A
; second pill during a fright finds the blue ones in state 2 and skips them
; too, which is right -- they already carry the speed and the art -- and frtk
; above has already been reloaded, which is the restart the arcade does.
; DESIGN: D1 holds the level row for the whole loop while D2 walks the
; records. Nothing between them touches D1: every write below is to a port,
; not to memory. So the frightened speed is read out of the row each time
; round rather than copied into a scalar first.
; DESIGN: no art here. This writes state bytes and the draw reads them; see
; adstrip. It used to point the latches at the blue strip and redefine each
; ghost's sprite as it went, which is the habit that let placeghosts forget.
; DESIGN: a ghost still in the HOUSE is skipped here and picked up later, not
; missed. ashfree reads frtk as it steps clear of the door and walks out
; frightened if a window is still open, so this routine owns the ghosts that
; are already out and ashfree owns the ones that arrive after.
; DESIGN: the chain of ghost values is reloaded HERE, on the pill, and nowhere
; else. It belongs to the pill and not to the level, not to the window and not
; to the fright clock: a window that expires with two ghosts uneaten leaves the
; chain where it stood, and the next pill is what starts it again at 200. It is
; reloaded ahead of the duration test below, so a level whose fright duration
; is zero -- 17, 19, 20 and 21 carry one -- leaves the chain in the same state
; every other level leaves it in, rather than in the last pill's.
frighten: LD A <- 1
        LD [modefl] <- A
        LD A <- 20
        LD [ghchain] <- A
        LD D1 <- [lvlptr]
        LD A <- [D1+3]       ; this level's fright duration, in units of 8 frames
        JZ frxdone           ; zero: turned round, and nothing else
        LD [frtk] <- A
        LD D2 <- blinky
        LD A <- 1
        LD [frg] <- A
frxl:   LD A <- [D2+5]
        CMP A, 1
        JNZ frxn           ; out of the house and normal: this one turns
frxblue: LD A <- 2
        LD [D2+5] <- A
        LD A <- [D1+2]       ; the level's frightened speed
        LD [D2+4] <- A
frxn:   LD D2 <- D2+8
        LD A <- [frg]
        INC A
        LD [frg] <- A
        SUB A <- 5
        JNZ frxl
frxdone: RET

; --- the window shuts: every blue ghost comes back, at its own speed and in
; its own colours.
; DESIGN: state 2 exactly, not "anything but 1". A ghost in the house is 0 and
; never turned; an eaten ghost is 3, on its way home as eyes, and a window
; shutting under it must leave it exactly as it is. The equality is what keeps
; that true, and that state exists now rather than being expected.
; DESIGN: three callers, and each is a moment a fright must not survive. This
; is not a list of places that happen to call it: it is the whole of what
; ends a fright. frstep above, when the clock runs out. drawmaze, because the
; countdown is per level and a maze that began owing a fright would count one
; down over ghosts the new level placed. resetact, because a ghost put back in
; the house still carrying state 2 would hunt at the frightened speed for the
; rest of the game.
; DESIGN: it zeroes frtk itself, so a caller cannot reset the clock and leave
; a ghost blue.
; DESIGN: no art here either. The draw reads the state byte this writes; see
; adstrip.
unfright: LD A <- 0
        LD [frtk] <- A
        LD D2 <- blinky
        LD A <- 1
        LD [frg] <- A
ufl:    LD A <- [D2+5]
        CMP A, 2
        JNZ ufn
ufback: LD A <- 1
        LD [D2+5] <- A
        LD A <- [ghspd]      ; this level's ghost speed, the one ghcom hands out
        LD [D2+4] <- A
ufn:    LD D2 <- D2+8
        LD A <- [frg]
        INC A
        LD [frg] <- A
        SUB A <- 5
        JNZ ufl
ufdone: RET

; --- the reversal a mode change owes, offered to one actor.
; DESIGN: here, ahead of the speed accumulator, and NOT in the centred branch
; further down. A ghost at 191/256 of a pixel a frame crosses a tile in about
; eleven frames and stands on its centre for one or two of them, and asturn is
; reached only when it is centred AND its accumulator carries that frame.
; Measured on this build over the 5795 frames of an idle run with all four
; ghosts out of the house: a ghost is centred on 12.4 to 12.6 percent of
; frames and centred with a carry on 9.3, and all four are in either state on
; the same frame exactly 0 times. So a reversal offered only at a centre would
; turn nobody at all on most flips and never the whole four. Held open until
; each of them next reached a centre it would turn them one at a time over the
; following ten frames, which is not a reversal of the group and is the shape
; that lets a flag outlive its moment.
; Turning off centre is what the arcade does and it is safe here: the ghost
; walks back over the tile it has just crossed, and asgo's off centre path
; takes its pixel with no wall test at all, which is exactly why it can.
; DESIGN: what a flip LOOKS like is a half turn on most ticks and a quarter
; turn on a few, and the comment used to describe only the first. Running
; ahead of the accumulator means a ghost that IS centred and carrying on the
; flip frame reaches pickdir immediately after being turned, and chooses again
; from the new facing. pickdir excludes the reverse of that facing, which is
; the direction the ghost arrived on, so it can never carry straight on and
; the reversal is never undone; what it can do is leave on a perpendicular.
; Measured over 7 flips of the same idle run, 28 ghost turns: 27 exact
; opposites and one quarter turn, Pinky at frame 1524 going 2 to 3 with a
; centre and a carry. A centre and a carry do not force it. At frame 3140
; Pinky had both and still turned 1 to 3.
; DESIGN: Pac-Man is asked and declines. The test is on acti and not on a
; field of the record, because a forced reversal belongs to being a ghost; he
; reaches here with state 1, the same state every ghost out of the house
; carries. A ghost still in the house never reaches here at all: actstep sends
; state 0 to ashwait above, so it neither takes the reversal nor holds it up.
modeturn: LD A <- [acti]
        JZ mtno              ; Pac-Man: not his to take
        LD A <- [modefl]
        JZ mtno
        LD A <- [adir]
        XOR A <- 2           ; a reversal is the direction exclusive-or 2
        LD [adir] <- A
mtno:   RET

; --- point D1 at the release countdown of the actor in the window.
; DESIGN: five bytes indexed by acti, not four indexed by acti minus one.
; Pac-Man's entry costs one byte and is never read, and in exchange the index
; is the walk's own counter with nothing to subtract off it first. There is no
; add for a D register either way, so an index is a walk: this is tileptr's
; shape, four steps at most.
houptr: LD D1 <- houstk
        JMP actidx           ; a tail call: actidx's RET answers this caller

; --- and at this actor's revive mark. See ehin, which sets it, and ashfree,
; which spends it.
revptr: LD D1 <- revive
        JMP actidx

; --- and at the strip this actor's sprite is currently wearing. See adstrip.
strptr: LD D1 <- astrip
        JMP actidx

; --- step D1 forward by acti, so a per-actor array is indexed by the actor
; the walk is on. One routine and not one per array: there is no indexed load,
; so a base in D1 and a count in acti is the whole of the addressing this
; machine offers, and both arrays want exactly that walk.
actidx: LD A <- [acti]
        JZ aixdone
        LD [t3] <- A
aixstep: INC D1
        LD A <- [t3]
        SUB A <- 1
        LD [t3] <- A
        JNZ aixstep
aixdone: RET

; --- a ghost in the house, walking out. One pixel a tick, and it asks the
; maze nothing: the ghost is not choosing anything, so there is no target to
; set and no candidate to test. First across to the door column, then out
; through the door the way outdy points, and out of state 0 once it stands a
; full tile clear of the door.
; DESIGN: the column it leaves by is doorx + 8, the RIGHT of the two door
; cells, so the vertical leg runs over a door cell and not over the house's
; own wall. Either cell would serve. The right one is where placeghosts
; already stands Pinky: all four shipped mazes put the door at columns 13 and
; 14 and the H at column 14, which the house tests pin against the maze text,
; so the three start on columns 13, 14 and 15 and two of them have exactly one
; tile to cross.
; DESIGN: adir is written on every step, so the sprite faces the way it walks
; and the direction it is holding when it reaches the outside is the outward
; one. pickdir never picks a reversal, so the first choice it makes out there
; cannot be straight back at the door.
; DESIGN: no canmove, no wrap, no advance. The house interior IS ordinary
; open ground to the maze (its tiles are spaces, and cmwall calls anything
; that is not '#' open), so canmove would answer and answer yes; what it
; cannot do is aim, and a ghost that asked pickdir in here would steer at
; Pac-Man through the house wall. The script is the aiming.
ashwalk: LD A <- [doorx]
        ADD A <- 8
        LD [t3] <- A         ; the column to leave by
        CMP A, [ax]
        JZ ashout            ; already on it: head out
        JC ashlf             ; borrow: the column is to the left of here
        LD A <- [ax]
        ADD A <- 1
        LD [ax] <- A
        LD A <- 0            ; right
        LD [adir] <- A
        RET
ashlf:  LD A <- [ax]
        SUB A <- 1
        LD [ax] <- A
        LD A <- 2            ; left
        LD [adir] <- A
        RET
; out through the door. Read from outdy, never assumed: three of the four
; shipped mazes hold the house below the door, so out is up, and Metro
; Station holds it above, so out is down there.
ashout: LD A <- [outdy]
        CMP A, 8
        JZ ashdn
        LD A <- [ay]
        SUB A <- 1
        LD [ay] <- A
        LD A <- 3            ; up
        LD [adir] <- A
        JMP ashout1
ashdn:  LD A <- [ay]
        ADD A <- 1
        LD [ay] <- A
        LD A <- 1            ; down
        LD [adir] <- A
ashout1: LD A <- [doory]
        ADD A <- [outdy]     ; one full tile outside the door
        SUB A <- [ay]
        JZ ashfree
        RET
; --- a full tile clear of the door: the maze rules apply from here on.
; DESIGN: a ghost that was WAITING walks out INTO the fright if one is
; standing, rather than out as a normal ghost. On level 1 the window is 45 units, 360 frames, and Pinky's
; release is 8 ticks of the divider, 64 frames, so a pill eaten early puts a
; red ghost on a board of blue ones and it is the player who pays. The arcade
; brings it out blue.
; DESIGN: the WINDOW, not the record. This runs inside the ghost's own step,
; so astate and aspd are its fields for this frame and saveact writes them
; back on the way out. frighten writes records because it runs from Pac-Man's
; step and the ghosts are not loaded yet; here the ghost IS the one loaded.
; DESIGN: no art and no frame is chosen here, and none should be. actdraw
; takes both from the state byte this writes, and takes a frightened ghost's
; frame from frtk, one clock for all four, so a ghost released mid-flash is in
; step with the rest on the frame it appears.
; DESIGN: a ghost REVIVED from eyes takes none of that. Getting home resets it,
; so it leaves normal, at the ghost speed ehin already wrote and in the colours
; ehin already gave it back, and it can catch him on the frame it appears. Both
; halves are tested, and the pair is the point: a build that made revived
; ghosts normal by making every released ghost normal would break the waiting
; case above.
ashfree: JSR revptr
        LD A <- [D1]
        JZ ashfpill          ; merely waiting: a standing pill claims it
        LD A <- 0
        LD [D1] <- A         ; spent, and it belongs to this trip out alone
        JMP ashfnorm         ; eaten and brought home: out dangerous, mid-pill
ashfpill: LD A <- [frtk]
        JZ ashfnorm          ; no pill standing: out as a normal ghost
        LD A <- 2            ; state: frightened, like the ones already out
        LD [astate] <- A
        LD D1 <- [lvlptr]
        LD A <- [D1+2]       ; and at this level's frightened speed
        LD [aspd] <- A
        RET
ashfnorm: LD A <- 1          ; state: out, and on the maze rules from here on
        LD [astate] <- A
        RET

; --- have the eyes got home? The tile they arrive on is the one floodhome
; seeds the field at: one step inside the door, on the column ashwalk leaves
; by. The two read the same three bytes, so neither can drift from the other.
; DESIGN: an exact match on both pixel coordinates, not a tile compare. An
; actor takes one pixel at a time and a target tile is reached exactly, so the
; two equalities are the whole test and there is no rounding to get wrong.
; DESIGN: what arrival does is put the ghost back into state 0, which IS the
; house. Task 8's release timer then walks it out through ashwalk, and nothing
; here is a second exit: the countdown of a ghost that has already been
; released stands at zero, so the scripted walk begins on the next tick.
; DESIGN: the state goes back to 0 here and the art follows it on the same
; frame, the draw reading this byte at the end of this actor's own step. What
; waits in the house is a ghost and not a pair of eyes. See adstrip.
; DESIGN: the speed goes back here too, and it has to. ashfnorm writes the
; state and nothing else, so a ghost that left the house still carrying 255
; would hunt at full speed for the rest of the level.
eyehome: LD A <- [doorx]
        ADD A <- 8
        SUB A <- [ax]
        JZ ehrow
        RET
ehrow:  LD A <- [doory]
        SUB A <- [outdy]
        SUB A <- [ay]
        JZ ehin
        RET
ehin:   LD A <- 0
        LD [astate] <- A     ; in the house again, on the release timer
        LD A <- [ghspd]      ; and at this level's ghost speed, not the eyes'
        LD [aspd] <- A
; DESIGN: mark it as REVIVED, which is not the same as waiting. A ghost that
; was merely sitting in the house when a pill was eaten never stopped being a
; hunting ghost, and the arcade brings it out blue; ashfree owns that. A ghost
; that was eaten has been reset by getting home, and the arcade sends it back
; out dangerous even with the window still open. Nothing else can tell the two
; apart at the door, both being state 0 on the release timer by then.
; DESIGN: an array indexed by acti and not a field of the record, because it
; is not a property of the ghost. It belongs to one trip out of the house, and
; ashfree spends it on the way past.
; t3 is free here: revptr uses it as its own walk counter and nothing on this
; path holds it across the call. canmove, the other user on an actor's step,
; runs after eyehome and spends it inside one call.
        JSR revptr
        LD A <- 1
        LD [D1] <- A
        RET

; =========================== the road home ===========================
; A distance field, flooded outward from the house once per maze. homedist
; holds, for every tile, how many steps it is from the tile the eyes have to
; reach. eyedir then reads its four neighbours and steps to the smallest.
; DESIGN: a field, not a target. Eyes used to steer the way a hunting ghost
; steers: of the open ways out that are not a reversal, take the one whose
; tile is nearest the house as the crow flies. That is greedy and memoryless,
; and on Photo Opportunity it closed a ten tile cycle in the bottom pocket.
; Measured tile by tile, with a ghost put on each as eyes and run until it
; reached the door: 318 of 318 tiles of CLASSIC got home, 350 of 350 of
; Parking Lot, 282 of 282 of Metro Station, and 300 of 312 of Photo
; Opportunity. Downhill on a distance field cannot cycle, because every step
; lowers a number that stops at zero, so the field arrives from anywhere and
; on any maze anyone adds later. It is the lesson rowtab and sqtab already
; teach: when the hardware will not do the arithmetic, precompute it.
; DESIGN: a breadth-first flood, not a relaxation sweep. Relaxation walks all
; 868 tiles once per step of the maze's diameter, and the longest way home
; measured on the four shipped mazes is 39, 50, 36 and 30 steps: fifty passes
; over 868 tiles is the better part of a million instructions. A flood puts
; each tile on the queue once, so it is one visit per open tile the house can
; reach, measured at 320, 352, 284 and 314. floodhome costs 33,963
; instructions on CLASSIC against a draw of 139,606 without it.
; DESIGN: per level, like the dot count, the tunnel table and the screen
; clear, and for the reason drawmaze's own block gives: nextlvl draws every
; maze through this same routine, so state that must be fresh per maze and is
; reset by a caller instead only waits for that caller to forget.
; DESIGN: t1 and t2 are free here. hdfill is the only user of them and it runs
; from the tail of the draw, past the walk that openat and tileptr spend them
; inside. Nothing else in the flood touches either.
floodhome:
        JSR hdfill
; --- the seed: the tile the eyes have to reach, at distance zero. One step
; INSIDE the door, on doorx + 8, the column ashwalk leaves by. That is the
; exact pair eyehome tests for, so the field bottoms out where the arrival is
; declared and nowhere else.
        LD D1 <- p2t
        LD A <- [doorx]
        ADD A <- 8
        LD A <- [D1+A]
        LD [dcol] <- A
        LD A <- [doory]
        SUB A <- [outdy]
        LD A <- [D1+A]
        LD [drow] <- A
        JSR hdcell
        LD A <- 0
        LD [D1] <- A
        LD D2 <- hq
        LD [hqhh] <- D2      ; an empty queue is head standing on tail
        LD [D2] <- D1
        LD D2 <- D2+2
        LD [hqth] <- D2
; --- the flood. Pop a cell, offer each of its four neighbours one step more
; than it, and push the ones that had not been reached. A cell is written on
; the pass that reaches it and never again, so it enters the queue once.
; DESIGN: the queue holds cell ADDRESSES, two bytes each, not tile positions.
; A position would have to be turned into an address for every read and every
; write, and there is no indexed store, so writing one would mean walking a
; pointer along its row. The address is what the flood has in hand already.
hdloop: LD A <- [hqhh]
        SUB A <- [hqth]
        JNZ hdgo
hdlq:   LD A <- [hqhl]
        SUB A <- [hqtl]
        JZ hddone            ; the head has caught the tail: nothing is left
hdgo:   LD D2 <- [hqhh]
        LD D1 <- [D2]+       ; the cell, and the head steps past it
        LD [hqhh] <- D2
        LD [hcurh] <- D1
        LD A <- [D1]
        ADD A <- 1
        LD [hdd] <- A        ; what a neighbour of this one is worth
        LD D2 <- D1+1
        JSR hdvis            ; right
        LD D2 <- D1-1
        JSR hdvis            ; left
        LD D2 <- D1+30
        JSR hdvis            ; down, one field row on
        LD D2 <- D1-30
        JSR hdvis            ; up
        JMP hdloop
hddone: RET

; --- one neighbour of the cell in hcur. D2 holds its address, which the
; caller stepped off D1 with an address add, forward or back. D1 comes back
; unchanged, so the next neighbour steps off it again.
hdvis:  LD A <- [D2]
        CMP A, $FF
        JZ hdtake            ; open, and not reached yet
        RET
hdtake: LD A <- [hdd]
        LD [D2] <- A
        LD D1 <- [hqth]
        LD [D1] <- D2
        LD D1 <- D1+2
        LD [hqth] <- D1
        LD D1 <- [hcurh]
        RET

; --- every cell of the field, before the flood: 254 for a wall, 255 for a
; tile that is open and has not been reached.
; DESIGN: two markers and not one. The flood's single test, "is this cell
; still 255", then answers both "is it a wall" and "has it been reached", so
; the flood reads one array and work is read here and nowhere else in it.
; DESIGN: a distance must stay under 254 or it reads as a marker, and this is
; a real constraint on a maze rather than something the grid guarantees: a
; serpentine 28 by 31 could hold a corridor several hundred steps long. The
; four shipped mazes are nowhere near it, the longest way home on them being
; 39, 50, 36 and 30 steps.
; What a maze past that line does, measured rather than guessed. A distance of
; 254 reads back as the wall marker and 255 as unreached, so hdvis goes on
; finding those cells open, hdtake writes them and pushes them again, and the
; queue has no bound test on its tail. hq holds one entry per tile and dxtab
; starts 1736 bytes past it, so the re-pushes run the tail straight through
; dxtab, dytab, dx8tab, dy8tab, p2t, sqtab, lvltab, ghsttab and mazetab. That
; is the game's tables overwritten, not a pair of eyes taking the long way
; round. Measured with the queue cut to 200 entries: 90 of the 151 tests in
; pacman.test.ts go red, most of them nothing to do with the eyes.
; It is left unguarded, and the whole-field comparison in pacman.test.ts is
; what stands in for the guard: it walks every cell of every maze against a
; breadth-first distance of its own, so a maze that crossed the line goes red
; there. That covers anyone who runs the suite and nobody who adds a maze and
; only plays it.
; My reading, for whoever decides: the guard is worth its price, and the price
; is small. Two instructions after the LD [hdd] in hdgo, SUB A <- $FE and JZ
; hddone, stop the flood the moment a popped cell would hand out 254. The
; queue is monotone, so nothing behind that cell is any nearer and stopping
; loses nothing that could still be written. Measured by building it: 640
; instructions, taking the draw from 173,569 to 174,209. It buys the
; difference between the game's tables being overwritten and eyes past 253
; steps standing still, and standing still is a bug a person can see.
; It is not built here for two reasons, neither of them mine to settle alone.
; It spends draw budget, and it ships a branch no shipped maze can reach, so
; no test can kill it. That is the shape pickdir's own fallback is in, and
; that one was argued rather than assumed.
; DESIGN: the border is written here too, on every level, though it never
; changes. Once at startup would do, and then the field would have two owners,
; one of them a write no per-level test can see. That is the shape of reset
; this project has dropped three times. 152 border cells against 868 real ones
; is what the safety costs.
; DESIGN: it reads work, so the field is built from the maze the draw has just
; laid down, with the markers already blanked. Both door cells are spaces by
; then (see dmdoor), which is what lets the flood cross the door and reach the
; maze outside the house at all. Eyes cross it inward, so the flood must.
hdfill: LD D2 <- homedist
        JSR hdbord           ; the row above the maze
        LD D1 <- work
        LD A <- 31
        LD [t1] <- A
hdfrow: LD A <- $FE
        LD [D2]+ <- A        ; the sentinel left of the row
        LD A <- 28
        LD [t2] <- A
hdfcol: LD A <- [D1]+
        CMP A, $23         ; '#'
        JZ hdfw
        LD A <- $FF
        LD [D2]+ <- A
        JMP hdfn
hdfw:   LD A <- $FE
        LD [D2]+ <- A
hdfn:   LD A <- [t2]
        SUB A <- 1
        LD [t2] <- A
        JNZ hdfcol
hdfeol: LD A <- $FE
        LD [D2]+ <- A        ; and the sentinel right of it
        LD A <- [t1]
        SUB A <- 1
        LD [t1] <- A
        JNZ hdfrow
hdfbot: JSR hdbord           ; the row below the maze
        JSR hdbord           ; and the spare one under that
        RET
; one field row, all wall, at D2.
hdbord: LD A <- 30
        LD [t2] <- A
hdb1:   LD A <- $FE
        LD [D2]+ <- A
        LD A <- [t2]
        SUB A <- 1
        LD [t2] <- A
        JNZ hdb1
hdbdn:  RET

; --- D1 := the field cell for tile (dcol, drow).
; DESIGN: the field is 30 wide and 34 tall where the maze is 28 by 31: a one
; tile border of wall all round it, and one spare row under the bottom border.
; The border is what lets a step off the grid read a wall byte instead of the
; far end of the row above, so neither the flood nor eyedir needs a bounds
; test at all. Without it a maze with column 0 open on one row and column 27
; open on the row above would have those two joined by an edge that is not
; there, and the field would hand the eyes a step they cannot take.
; DESIGN: hrowtab holds all 34 field rows, so maze row r is field row r + 1
; and drow may be off the grid at either end: 255 reads field row 0 and 31
; reads field row 32, and both are border. Field row 33, the spare one, is
; there for drow 32: pactile turns any pixel row from 248 up into tile row 31,
; which is already past the maze's last row, and eytry then steps one further
; down from it. Without the spare that read would be off the end of the table.
; DESIGN: the byte adds wrap on purpose. dcol 255, a step left off column 0,
; plus the column shift of one is 0, the sentinel left of the row; dcol 28 is
; 29, the sentinel right of it.
hdcell: LD A <- [drow]
        ADD A <- 1           ; maze row r is field row r + 1
        SHL A                ; times two: the table holds words
        LD D2 <- hrowtab
        LD D1 <- [D2+A]      ; the field row's first cell
        LD A <- [dcol]
        ADD A <- 1           ; past the sentinel
        LD D1 <- D1+A
        RET

; --- the field's row table: 34 addresses, one per field row.
; DESIGN: built by adding the row width to a running address in one address
; add. There is nothing to read along the way here.
; DESIGN: once, at startup, not once per level. The shape of the field never
; changes; only its contents do, and floodhome rewrites those on every draw.
mkhrows: LD D1 <- homedist
        LD D2 <- hrowtab
        LD A <- 34
        LD [hct] <- A
mkhr:   LD [D2] <- D1
        LD D2 <- D2+2
        LD D1 <- D1+30
        LD A <- [hct]
        SUB A <- 1
        LD [hct] <- A
        JNZ mkhr
mkhrdn: RET

; --- what Pac-Man does with a centre: eat the tile under him, read the pad,
; and take the buffered turn if that way is open.
; DESIGN: one half of the per-actor hook actstep asks when an actor reaches a
; centre. The other half is ghdecide below. A ghost must neither eat nor read
; the pad, so the choice is a dispatch on which record is in the window rather
; than one routine with a test folded into it.
pacdecide: JSR eat
        JSR readpad
        LD A <- [pacnext]
        JSR canmove
        JZ pdkeep            ; that way is blocked: hold the direction
        LD A <- [pacnext]
        LD [adir] <- A
pdkeep: RET

; --- what a ghost does with a centre: aim, then choose.
; DESIGN: the other half of the per-actor hook asturn dispatches on. Pac-Man
; eats and reads the pad; a ghost sets a target and asks pickdir.
; DESIGN: a JMP, not a JSR and a RET. pickdir's own RET answers asturn.
; DESIGN: a frightened ghost is not aimed at all, so target never runs for it.
; The dispatch is on the state byte and not on a flag of its own: state 2 IS
; frightened, and a ghost in the house never reaches here at all, actstep
; sending state 0 to the scripted walk instead.
ghdecide: LD A <- [astate]
        CMP A, 2
        JZ frdir             ; frightened: a random turn, with nothing to aim at
        SUB A <- 3
        JZ eyedir            ; eaten: downhill on the field, not aimed at all
        JSR target
        JMP pickdir

; --- the eyes' step: downhill on the distance field floodhome laid down.
; DESIGN: a field, not a target, and so not pickdir either. target and dist2
; between them answer "which open way is nearest as the crow flies", which is
; the question a hunting ghost asks. It is the wrong question for a ghost that
; has to GET somewhere: on Photo Opportunity the answer closed a ten tile
; cycle in the bottom pocket and a pair of eyes eaten there never came home.
; homedist answers "how many steps from here to the house", so the smallest
; neighbour is a step along a road that really goes there.
; DESIGN: no reversal rule. Downhill cannot cycle on its own, so refusing the
; way back buys nothing, and it can only cost: on a tile whose nearest
; neighbour IS the way the eyes came, the rule refuses the one step that
; shortens the road and takes a longer one instead. It also has no business
; here now the target is gone -- it exists in pickdir to stop a hunting ghost
; dithering, and eyes do not dither, they walk down a number.
; Measured, because "it can only cost" is not the same as "it breaks":
; with the rule put back the eyes STILL reach the house from every tile of
; every maze. What it changes is the step taken, on tiles of all four mazes,
; which is why its absence is pinned by the two decision tests in
; pacman.test.ts and not by the sweep.
; DESIGN: no canmove call either. A wall reads 254 in the field and a tile the
; flood never reached reads 255, so neither is ever the smallest, and the
; field's border says the same about a step off the grid. The door is the one
; thing left to canmove, which passes eyes and refuses a living ghost; the
; flood runs straight through it, which is right, because eyes are the only
; thing that reads the field.
; DESIGN: ties go to the first tried, and the order tried is up, then left,
; then down, then right, the same tie-break pickdir uses. The comparison below
; is a strict "nearer", so a candidate that only equals the best never
; displaces it and the order alone decides.
; DESIGN: the best starts at 254 and not at 255. Starting at 255 would take a
; wall, 254 being the smaller of the two; 254 takes neither marker and every
; distance the flood can write.
; DESIGN: adir is the fallback, exactly as it is in pickdir. Reaching it needs
; all four neighbours to be wall or unreached, and no shipped maze puts a
; standable tile in that position: the sweep in pacman.test.ts walks eyes home
; from every one of them. The test that covers the fallback pokes the field by
; hand to build the tile no maze provides.
; DESIGN: gcol and grow are pickdir's own pair, reused rather than doubled. A
; ghost takes one arm of ghdecide or the other and never both, so the two
; routines are never live at the same moment.
eyedir: JSR pactile
        LD A <- [dcol]
        LD [gcol] <- A
        LD A <- [drow]
        LD [grow] <- A
        LD A <- $FE
        LD [hbest] <- A
        LD A <- [adir]
        LD [hbdir] <- A
        LD A <- 3            ; up, then left, then down, then right
        JSR eytry
        LD A <- 2
        JSR eytry
        LD A <- 1
        JSR eytry
        LD A <- 0
        JSR eytry
        LD A <- [hbdir]
        LD [adir] <- A
        RET
; one candidate, its direction in A.
eytry:  LD [hcand] <- A
        LD D1 <- dxtab
        LD A <- [hcand]
        LD A <- [D1+A]
        ADD A <- [gcol]
        LD [dcol] <- A
        LD D1 <- dytab
        LD A <- [hcand]
        LD A <- [D1+A]
        ADD A <- [grow]
        LD [drow] <- A
        JSR hdcell
        LD A <- [D1]
        SUB A <- [hbest]
        JC eytake            ; strictly fewer steps from home than the best
        RET
eytake: LD A <- [D1]
        LD [hbest] <- A
        LD A <- [hcand]
        LD [hbdir] <- A
        RET

; --- set tcol and trow for the ghost in the window.
; DESIGN: dispatch on acti, not on a field of the record, because the target
; rule belongs to which ghost this is and not to any state the ghost carries.
; There is no jump table on this machine, so it is a chain of compares, and
; Clyde is the fall-through rather than a fourth test of his own.
; DESIGN: every rule reads another actor's RECORD, at pacman or at blinky, and
; never the actor window. target runs with a ghost loaded, so ax, ay and adir
; are that ghost's, and pactile reads the window too. Both labels are
; assembly-time constants, so a record is one LD D1 away.
; DESIGN: the mode is read first, and in scatter none of the four rules runs
; at all. A ghost in scatter walks at ascol and asrow, the corner placeghosts
; wrote into its own record, which is the same pair Clyde's rule reaches for
; when he gets close, so the corner branch is one label shared by both.
; DESIGN: it does NOT get a ghost out of the house, and it is never asked to.
; A ghost in the house is in state 0, and actstep sends state 0 to the
; scripted walk at ashwalk instead of here: it neither targets nor chooses
; until it stands a full tile outside the door. That split is why this routine
; can read the maze's own rules and nothing else. Before the script existed
; the targets alone were what moved a housed ghost, and they moved it badly:
; with Pac-Man standing at his spawn below the house, all three of Pinky, Inky
; and Clyde steered down into the house's own bottom wall and circled the
; interior. Re-measured on ebb1e09, the commit before this one: on CLASSIC,
; Pinky is inside the interior on all 1200 calls of an idle run, and finishes
; it on (14, 11), still in there.
; DESIGN: the door is one way now, so the other half of that is closed too.
; Blinky spawns OUTSIDE, and with Pac-Man at his spawn the nearest step toward
; him is down through the door; measured on the build before the rule, Blinky
; was on CLASSIC's right-hand door cell by call 45 of an idle run and inside for
; 1155 of the next 1200 calls, a call being one runToNextFrame, about half a
; frame. canmove passes a ghost outward and never inward, so the rule can go on
; steering at Pac-Man through a wall without walking anyone in.
target: LD A <- [mode]
        JZ tgcorn            ; scatter: this ghost's own corner, nothing to aim
        LD A <- [acti]
        SUB A <- 1
        JZ tgblink
        LD A <- [acti]
        SUB A <- 2
        JZ tgpink
        LD A <- [acti]
        SUB A <- 3
        JZ tginky
        JMP tgclyde

; Blinky: Pac-Man's own tile, and nothing to clamp. Pac-Man is walked by the
; same canmove every ghost is, which blocks a step off the grid everywhere but
; a tunnel mouth, and wrap relocates him at a mouth before the step is taken,
; so his pixel position never leaves the maze and p2t answers with a column
; 0..27 and a row 0..30.
; A JMP, not a JSR: pacaim's RET answers ghdecide.
tgblink: JMP pacaim

; Pinky: four tiles along Pac-Man's facing. The target leaves the board
; whenever Pac-Man is within four tiles of the edge he faces. On CLASSIC that
; is most of the outer ring: rows 1 and 29 run the full width, and columns 1
; and 26 run in broken stretches, walled at rows 9, 10, 12, 13, 15, 16, 18,
; 19, 24 and 25. So the clamp sits on the ordinary path, not the corner one.
tgpink: JSR pacaim
        LD A <- 4
        LD [tgn] <- A
        JSR tgahead
        JMP clamp

; Inky: two tiles ahead of Pac-Man is P, and the target is Blinky reflected
; through it, 2P minus B.
; DESIGN: P is clamped BEFORE it is doubled, and the difference clamped after.
; The first of the two is defensive, and measured to be: brute-forced over
; every reachable pose -- Pac-Man's 28 by 31 tiles, his four facings, Blinky's
; 28 by 31 -- deleting it changes not one target, because the clamp on the
; difference absorbs the doubled wrap as well. It stays for the reason the row
; clamp in dist2 stays. What makes it unobservable is an accident of two
; numbers, the 128 threshold and a board under 64 tiles wide, and neither is
; this routine's to promise: doubling a P that has wrapped past zero doubles
; the wrap with it, and P is a tile on the board only because this call says
; so. No test kills it, and none can while both numbers hold.
tginky: JSR pacaim
        LD A <- 2
        LD [tgn] <- A
        JSR tgahead
        JSR clamp
        LD D1 <- blinky
        LD A <- [D1]
        LD [bcol] <- A
        LD A <- [D1+1]
        LD [brow] <- A
        LD D1 <- p2t
        LD A <- [bcol]
        LD A <- [D1+A]
        LD [bcol] <- A
        LD A <- [brow]
        LD A <- [D1+A]
        LD [brow] <- A
        LD A <- [tcol]       ; 2P minus B, one axis at a time
        SHL A
        SUB A <- [bcol]
        LD [tcol] <- A
        LD A <- [trow]
        SHL A
        SUB A <- [brow]
        LD [trow] <- A
        JMP clamp

; Clyde: Pac-Man, until he gets close, and then his own scatter corner.
; DESIGN: "closer than 8 tiles" is a strict comparison, so eight tiles exactly
; CHASES. The rule table in docs/pacman-design.md reads "his corner when
; closer than 8 tiles", and the arcade compares the same way: a squared
; distance of 64 or more targets Pac-Man. dist2 works in squares, so 64 is the
; number and there is no square root to take.
; DESIGN: pactile here, in the caller, and never inside dist2. dist2 measures
; from whatever dcol and drow hold and every call site owns setting them.
tgclyde: JSR pacaim          ; Pac-Man's tile: the target, and the yardstick
        JSR pactile          ; dcol, drow: Clyde's own tile, out of the window
        JSR dist2
        LD A <- [d2lo]       ; a sixteen bit compare, low byte then high
        SUB A <- 64
        LD A <- [d2hi]
        SBC A <- 0
        JC tgcorn            ; borrow out of the pair: under 64, so closer
        RET
; the scatter corner, for the two rules that want it: Clyde inside eight tiles,
; and every ghost while the clock says scatter.
tgcorn: LD A <- [ascol]
        LD [tcol] <- A
        LD A <- [asrow]
        LD [trow] <- A
        RET

; --- Pac-Man's tile into tcol and trow, and his facing into pmdir.
; DESIGN: tcol and trow are their own scratch. The two pixel bytes land in
; them and are replaced in place by the two tile bytes, which saves a pair of
; scalars and costs nothing, because that is where the answer is going anyway.
; DESIGN: D1 is loaded once for p2t and serves both axes, the way pactile does
; it: a pixel is a pixel whichever axis it came off.
pacaim: LD D1 <- pacman
        LD A <- [D1+2]
        LD [pmdir] <- A
        LD A <- [D1]
        LD [tcol] <- A
        LD A <- [D1+1]
        LD [trow] <- A
        LD D1 <- p2t
        LD A <- [tcol]
        LD A <- [D1+A]
        LD [tcol] <- A
        LD A <- [trow]
        LD A <- [D1+A]
        LD [trow] <- A
        RET

; --- step the tile in tcol, trow along Pac-Man's facing, tgn times.
; DESIGN: repeated addition, because there is no multiply, and the two callers
; ask for four steps and two. dxtab and dytab are the tables canmove and
; advance already walk, read through D1 rather than as a zero page address.
tgahead: LD A <- [tgn]
        JZ tgadone
        SUB A <- 1
        LD [tgn] <- A
        LD D1 <- dxtab
        LD A <- [pmdir]
        LD A <- [D1+A]
        ADD A <- [tcol]
        LD [tcol] <- A
        LD D1 <- dytab
        LD A <- [pmdir]
        LD A <- [D1+A]
        ADD A <- [trow]
        LD [trow] <- A
        JMP tgahead
tgadone: RET

; --- bring tcol and trow back inside the grid: column 0..27, row 0..30.
; DESIGN: one routine, three call sites. Pinky's four steps, Inky's P and
; Inky's 2P minus B are the three places a target is computed rather than
; read, and every one of them can land either side of the board.
; DESIGN: clamped, not wrapped. A target off the board is legal in the arcade
; and it is what pulls a ghost toward an edge; clamping keeps that pull, where
; wrapping would send Pinky chasing the opposite side of the maze. It also
; keeps both legs dist2 measures inside the 0..63 that sqtab covers.
; DESIGN: 128 is where "went below zero" is read off, because a byte cannot be
; negative. Off the left or the top lands at 226 to 255 -- Inky's difference
; bottoms out at 2*0 minus 30 -- while the largest an honest coordinate
; reaches is 2*30, which is 60. Nothing this routine is handed falls between
; 61 and 225, so one threshold separates the two cases outright.
clamp:  LD A <- [tcol]
        CMP A, 128
        JC clcmax            ; borrow: under 128, a real column
        LD A <- 0            ; 128 and up: it went off the left
        LD [tcol] <- A
        JMP clrow
clcmax: LD A <- [tcol]
        CMP A, 28
        JC clrow             ; 0..27 already
        LD A <- 27
        LD [tcol] <- A
clrow:  LD A <- [trow]
        CMP A, 128
        JC clrmax
        LD A <- 0            ; off the top
        LD [trow] <- A
        RET
clrmax: LD A <- [trow]
        SUB A <- 31
        JC clrdone           ; 0..30 already
        LD A <- 30
        LD [trow] <- A
clrdone: RET

; --- zero when both coordinates are multiples of 8.
; DESIGN: a tile is 8 pixels, so the test is two ANDs and no arithmetic.
atcentre: LD A <- [ax]
        AND A <- 7
        JZ accy
        RET                  ; nonzero: off centre
accy:   LD A <- [ay]
        AND A <- 7
        RET

; --- A holds a direction. Returns A nonzero when that way is open.
; DESIGN: dcol can land on 255 or 28 after the offset, and drow likewise, at
; a tunnel mouth on an outer column. tileat must never be asked to read that:
; with no bounds check it silently returns some other row's byte instead of
; failing (reviewer's trace: dcol 255 on CLASSIC's row 14 reads work+647, row
; 23 column 3, a dot, so the old code reported the way open and let him glide
; off the visible maze). Out of range while STANDING on a mouth still counts
; as open: it is a step into the tunnel, and wrap is what relocates the actor
; a moment later. Out of range anywhere else is blocked outright, not because
; every maze's outer edge is wall (maze3 and maze4 both open onto row 0, row
; 30, or a side column away from any mouth: verified against the maze data,
; not assumed) but because this check does not ask what a maze's edge looks
; like at all: it blocks every out-of-range position except one the actor is
; standing on a mouth for, regardless of whether some other edge is open too.
; DESIGN: OVERRIDE. It asks the whole table through tunfind, not one recorded
; row and two recorded columns. Three of the four shipped mazes declare two
; pairs, and Photo Opportunity's second joins row 4 to row 27, so there is no
; single tunnel row to compare against any more.
canmove: LD [t3] <- A
        JSR pactile          ; dcol, drow := his tile
        LD D1 <- dxtab
        LD A <- [t3]
        LD A <- [D1+A]
        ADD A <- [dcol]
        LD [dcol] <- A
        LD D1 <- dytab
        LD A <- [t3]
        LD A <- [D1+A]
        ADD A <- [drow]
        LD [drow] <- A
; --- second entry point: the tile to test is already in dcol and drow, and
; the window still says whose move it would be.
; DESIGN: a label, not a copy. pickdir steps dcol and drow one tile toward a
; candidate itself, because it then hands that same stepped tile to dist2 to
; be measured; entering at canmove would make it step from the actor's own
; position a second time, and computing the tile twice is how the two copies
; drift apart. Everything below this line reads dcol, drow, ax, ay and acti
; and never t3, so the split costs the routine nothing: it is the same body
; either way in.
cantile: LD A <- [drow]
        CMP A, 31
        JNC cmedge             ; row 0..30: check the column next
cmcol:  LD A <- [dcol]
        CMP A, 28
        JC cmtile            ; column 0..27 too: safe to read the tile
; NOTE: the cmyes this reaches changes no actor's path in this build, and
; saying so is the point of writing it out. wrap runs before canmove now, so
; an actor standing on a mouth is relocated before this is ever asked about
; the step into the tunnel, and a ghost that reaches a mouth with nothing else
; open keeps its direction on pickdir's fallback and warps either way.
; Measured, not reasoned: replacing this whole branch with "cmedge: JMP cmno"
; leaves all 82 tests green AND produces a byte-identical trace of all five
; actors' position, direction and state over 3000 samples on each of the four
; mazes, 12,000 samples, sampled on the frame loop's own entry with the pad
; driven left, up, right, down every 137 samples so the ghosts keep moving.
; It stays because it is what keeps dist2's column clamp REACHABLE: this is
; the only route by which a candidate tile off the grid gets as far as being
; measured, and with the branch closed that clamp is dead code and the test
; that names it passes on a build with the clamp deleted. Measured that way
; too: deleting the clamp on this build turns that one test red, and only it.
cmedge: JSR tunfind
        JZ cmno              ; off the grid and not from a mouth: a wall
        JMP cmyes
; --- the ghost house door: a wall to Pac-Man, and ONE WAY for a ghost. Out
; always, in never. That is the whole of what keeps Pac-Man out of the house
; and the whole of what keeps a ghost that has left from wandering back in.
; Nothing in the movement code knows the house is there at all: housex and
; housey are read only by the draw and by placeghosts.
; DESIGN: one way, not open to ghosts. It used to pass a ghost both ways, and
; the cost was visible on an idle board: Pac-Man spawns below the house, so
; the nearest step toward him from Blinky's spawn is down through the door,
; and Blinky walked in and circled the interior. Measured on the build before
; this rule, the tile (12, 14) -- CLASSIC's right-hand door cell -- from call
; 45 of an idle run onward, and inside for 1155 of the next 1200 calls. The
; unit is the test harness's runToNextFrame, which is about half a frame.
; DESIGN: eyes are the one exception, and they are a second state tested here
; rather than a second rule. A ghost that has been eaten is state 3 and is
; going home; the door stays shut to a LIVING ghost heading in, which is the
; whole of what the one-way rule was for.
; DESIGN: the recorded position, not the tile. drawmaze blanks both door cells
; to spaces as it draws (see dmdoor), so work holds nothing to test against and
; a maze character would have to be a wall to everyone or open to everyone.
; doorx and doory are what identify them.
; DESIGN: two cells, not one. dmdoor records only the first it meets, which is
; the left one, so the door is doorx and doorx + 8 on row doory. All four
; shipped mazes put the pair at columns 13 and 14 of a row, which the house
; tests pin against the maze text.
; DESIGN: doorx and doory are pixels, like every recorded coordinate, and dcol
; and drow are tile numbers, so the comparison multiplies by 8. There is no
; shift, so that is three adds, and the range check above has already bounded
; drow at 30 and dcol at 27, so neither product overflows a byte. t1 is
; canmove's own scratch: no caller of canmove or of cantile holds anything in
; it across the call. It used to be free for a different reason, that the
; pactile call above spent it anyway, and the table pactile no longer touches
; it at all.
; DESIGN: every actor runs the position test and only the answer differs, so
; the two door cells are identified in one place rather than once per actor.
; doorx zero means this maze recorded no door at all, the same flag dmdoor
; uses. No shipped maze is like that, so this only stops the rule from reading
; tiles (0, 0) and (1, 0) as a door on a maze that has none.
cmtile: LD A <- [doorx]
        JZ cmwall
        LD A <- [drow]
        SHL A
        LD [t1] <- A
        ADD A <- [t1]
        LD [t1] <- A
        ADD A <- [t1]        ; drow times 8: the entered row, in pixels
        CMP A, [doory]
        JNZ cmwall
cmdcol: LD A <- [dcol]
        SHL A
        LD [t1] <- A
        ADD A <- [t1]
        LD [t1] <- A
        ADD A <- [t1]        ; dcol times 8: the entered column, in pixels
        LD [t1] <- A
        CMP A, [doorx]
        JZ cmdoor            ; the left door cell
        LD A <- [t1]
        SUB A <- [doorx]
        CMP A, 8
        JNZ cmwall            ; and the right one
; --- the tile being entered IS one of the two door cells.
; DESIGN: which side the actor is stepping FROM decides it, not which way it
; is facing. canmove is only ever asked from a tile centre (asturn and
; pacdecide both sit behind atcentre, and pickdir scores a candidate one tile
; from the ghost's own), so ay is exact, and doory minus outdy is the tile one
; step inside the door. A ghost standing there is on its way out and passes; a
; ghost anywhere else, which on a shipped maze means the tile one step
; outside, does not. t3 would have been the alternative -- canmove's own copy
; of the direction -- but cantile is entered with t3 stale (see the note
; above), so the position is what both entries can agree on.
; NOTE: the cmyes below is DEAD CODE in this build, and saying so is the point
; of writing it out. Leaving the house is the scripted walk at ashwalk, which
; moves pixels itself and never asks canmove, so nothing here ever crosses the
; door outward. Measured, not reasoned: replacing this whole branch with
; "cmdoor: JMP cmno" leaves all 72 tests green AND produces a byte-identical
; trace of every ghost's position and state byte over 900 frames on each of the
; four mazes, 3600 samples. Sampled at the frame loop entry, which matters: a
; sample taken on a wall-clock frame boundary instead moves with the
; instruction count, and the shorter build then reads 13 of the 3600 one pixel
; apart, which is the harness and not the machine. It is an equivalent mutant,
; not a coverage gap.
; It is written out anyway because the rule is one way rather than shut, and
; the two are different rules that happen to agree today. Task 16's returning
; eyes did not change that, and the prediction made here before they existed
; held: eyes arrive from OUTSIDE, at doory + outdy, where this test only ever
; passes doory - outdy, so they take the state 3 branch above instead and this
; line stayed dead. A ghost revived inside the house leaves on ashwalk with
; the rest, which never asks canmove at all.
cmdoor: LD A <- [acti]
        JZ cmno              ; actor 0 is Pac-Man: the door is a wall to him
        LD A <- [astate]
        CMP A, 3
        JZ cmyes             ; eyes, going home: the one thing that crosses in
        LD A <- [doory]
        SUB A <- [outdy]     ; the tile one step inside the door
        CMP A, [ay]
        JZ cmyes             ; a ghost on it is leaving, and may cross
        JMP cmno             ; any other ghost is heading in, and may not
cmwall: JSR tileat
        CMP A, $23         ; '#'
        JZ cmno
cmyes:  LD A <- 1
        RET
cmno:   LD A <- 0
        RET

; --- the windowed actor's pixel position as a tile.
; DESIGN: a table, not a division. This walked ax and ay down eight at a
; time, up to 31 steps of six instructions each, and it is on the hot path:
; canmove calls it on every wall test and pickdir once more per ghost that
; reaches a tile centre. Counted on a breakpoint: 2.98 calls a walk on
; average, and 11 on a walk where all five actors reach a centre together.
; p2t is 256 bytes that answer both halves with one indexed load each.
; The measurement, and the harness in full, because a figure nobody can
; reproduce is not evidence. Naive microcode, fast-frame on with a zero gap,
; settle of runBudget(200000) then 40 runToNextFrame with nothing pressed,
; THEN left held for the rest of the run, breakpoints on actall and on the
; JMP main that follows it, 1000 consecutive walks. When the press lands and
; how many walks are sampled both matter: pressing before the settle instead
; moves the mean by about 3 percent, and 300 walks is phase sensitive where
; 1000 is not.
;   this build                        691.37 a walk, worst 1878
;   the same build, walk restored    1363.34 a walk, worst 4307
;   b9a49e3, before the ghosts chose 1061.76 a walk, worst 1557
; So the table is worth 671.97 instructions a walk on the same build, and the
; walk is cheaper after this task than before it, ghosts and all: pactile is
; on Pac-Man's own path three times a centred frame, so the table pays for
; the whole of pickdir and hands a third of the walk back on top.
; DESIGN: D1 is loaded once and serves both lookups, because the table is
; the same table for either axis: a pixel is a pixel.
pactile: LD D1 <- p2t
        LD A <- [ax]
        LD A <- [D1+A]
        LD [dcol] <- A
        LD A <- [ay]
        LD A <- [D1+A]
        LD [drow] <- A
        RET

; --- squared distance from the tile in dcol, drow to the target in tcol,
; trow, left in d2hi:d2lo.
; DESIGN: no multiply, so each leg is a lookup in sqtab and the two squares
; are added as a sixteen bit pair. Squared, so there is no square root and no
; comparison the CPU cannot do: nearer squared is nearer. A leg is clamped to
; 63, the table's last entry and wider than any maze, so a difference that big
; scores as 63 rather than reading past the table.
; DESIGN: the two clamps are not equals, and only the column one fires today.
; cantile has already bounded drow to 0..30 before a candidate reaches here,
; and every target row is 0..30 too, so the row leg never passes 30 and its
; clamp is defensive: deleting it passes the whole suite, and no test can kill
; it. The column leg does reach 255, at a tunnel mouth on an outer column,
; where cantile counts a step off the grid as open; that clamp is live and
; pinned by "scores a step off the grid as far away" -- deleting it turns that
; test red on this build and no other, measured after task 14. The row clamp
; is still defensive after task 6, and deliberately so. That task brought the
; computed targets this sentence used to promise -- Pinky's four steps and
; Inky's 2P minus B -- and both of them do run off the board, but target
; clamps every one of them into 0..27 by 0..30 before dist2 is ever called,
; because a leg past 63 reads past the table whichever axis it is on and the
; column half proves what that costs.
; DESIGN: OVERRIDE. Measures from whatever dcol and drow already hold, and
; never calls pactile. The caller owns them. pickdir steps dcol and drow one
; tile toward the candidate it is scoring and then calls this, so a pactile
; here would overwrite that step with the ghost's own tile, score all three
; candidates identically and hand the choice to whichever was tried first.
; Every call site sets both before the call. There are two: pdtry, and
; tgclyde, which calls pactile ITSELF, one line above, to measure Clyde's own
; tile against Pac-Man's.
dist2:  LD A <- [dcol]       ; the column leg, as an absolute difference
        SUB A <- [tcol]
        JNC d2xabs            ; borrow: the target column is the larger
d2xneg: LD A <- [tcol]
        SUB A <- [dcol]
d2xabs: LD [d2leg] <- A
        CMP A, 64
        JC d2xsq             ; borrow: 0 to 63 already, nothing to clamp
        LD A <- 63
        LD [d2leg] <- A
d2xsq:  LD A <- [d2leg]
        SHL A     ; twice the leg: the table holds words
        LD D1 <- sqtab
        LD D2 <- [D1+A]
        LD [d2hi] <- D2      ; the running total starts at the column square
        LD A <- [drow]       ; and the row leg, the same shape
        SUB A <- [trow]
        JNC d2yabs
d2yneg: LD A <- [trow]
        SUB A <- [drow]
d2yabs: LD [d2leg] <- A
        CMP A, 64
        JC d2ysq
        LD A <- 63
        LD [d2leg] <- A
d2ysq:  LD A <- [d2leg]
        SHL A
        LD D1 <- sqtab
        LD D2 <- [D1+A]
        LD [d2shi] <- D2
; add the row square in, low byte first so the carry runs the right way. A
; byte load leaves C alone, which is what lets the ADC below see the ADD's
; carry: the same chain the mul8 demo runs on.
        LD A <- [d2lo]
        ADD A <- [d2slo]
        LD [d2lo] <- A
        LD A <- [d2hi]
        ADC A <- [d2shi]
        LD [d2hi] <- A
        RET

; --- choose this ghost's direction: of the three that are not a reversal,
; keep the ones the maze allows, and take the one whose tile is nearest
; (tcol, trow).
; DESIGN: ties go to the first tried, and the order tried is up, then left,
; then down, then right, which is the arcade's own tie-break. The comparison
; below is a strict "nearer than", so a candidate that only equals the best
; never displaces it and the order alone decides.
; DESIGN: the best distance starts at $FFFF, which no candidate can reach.
; Both legs clamp at 63, so the largest sum sqtab can produce is 3969 + 3969,
; which is 7938. That makes "nothing chosen yet" and "worse than the best"
; the same test, and saves a flag.
; DESIGN: adir is the fallback, so a ghost with nothing open keeps the
; direction it had. That is a wall it has already been told about, and
; asturn's own canmove is what stops it a moment later, rather than walking
; the ghost into it.
; CLOSED, and this note says how, because it used to say the opposite. The
; fallback used to mean standing still forever: reaching it needs all three
; non-reversal directions to be wall, which is a dead end, and mazes 2, 3 and
; 4 each carried two of them -- a corridor ending on a tile marked '2', a
; second tunnel pair drawmaze read as neither wall nor floor. Traced at the
; time: on maze 4, tile (27, 4), Blinky stood still for 995 of 1500 frames.
; Task 14 read every tunnel digit instead, and a flood fill from each maze's
; own spawn now finds 2, 4, 4 and 4 reachable dead ends across the four
; mazes and every single one of them is a tunnel mouth, which wrap moves an
; actor off before the fallback direction is ever taken.
; So the fallback is now UNREACHABLE rather than merely untested, and no test
; can kill it: replacing the LD A <- [adir] below with a constant still passes
; all 82, re-measured after task 14, and the ghost tests now run all four
; mazes rather than CLASSIC alone.
; It stays because "keep the direction you had" is the honest answer to "every
; way is shut", and because a later maze, or a mode that shuts a way this one
; does not, can put a ghost back in front of it.
; DESIGN: the ghost's own tile is read once, into gcol and grow, and each
; candidate is that plus one step. pactile is cheap now, but canmove is not,
; and entering it at cantile with the tile already stepped is what lets the
; same stepped tile be handed straight to dist2 without computing it twice.
pickdir: JSR pactile
        LD A <- [dcol]
        LD [gcol] <- A
        LD A <- [drow]
        LD [grow] <- A
        LD A <- $FF
        LD [bsthi] <- A
        LD [bstlo] <- A
        LD A <- [adir]
        LD [bstdir] <- A
        LD A <- 3            ; up, then left, then down, then right
        JSR pdtry
        LD A <- 2
        JSR pdtry
        LD A <- 1
        JSR pdtry
        LD A <- 0
        JSR pdtry
        LD A <- [bstdir]
        LD [adir] <- A
        RET

; one candidate, its direction in A.
pdtry:  LD [pdcand] <- A
        XOR A <- 2           ; a reversal is the direction exclusive-or 2
        SUB A <- [adir]
        JZ pdno
        LD D1 <- dxtab
        LD A <- [pdcand]
        LD A <- [D1+A]
        ADD A <- [gcol]
        LD [dcol] <- A
        LD D1 <- dytab
        LD A <- [pdcand]
        LD A <- [D1+A]
        ADD A <- [grow]
        LD [drow] <- A
        JSR cantile
        JZ pdno              ; a wall, or off the grid away from a mouth
        JSR dist2            ; measures the candidate tile, still in dcol/drow
; nearer than the best so far? A sixteen bit subtract, low byte then high with
; the borrow carried in, and C at the end is the borrow out of the pair: set
; exactly when this candidate is the smaller.
        LD A <- [d2lo]
        SUB A <- [bstlo]
        LD A <- [d2hi]
        SBC A <- [bsthi]
        JC pdtake
pdno:   RET
pdtake: LD A <- [d2hi]
        LD [bsthi] <- A
        LD A <- [d2lo]
        LD [bstlo] <- A
        LD A <- [pdcand]
        LD [bstdir] <- A
        RET

; --- a frightened ghost's choice: random, and not aimed.
; DESIGN: a random start on a cyclic scan, not a uniform draw from whatever is
; open. There is no divide on this machine, so "the second of the three open
; ways" cannot be turned into an index; what this does instead is take two
; random bits as a starting direction and step round until one is legal. The
; bias it carries, and it is a real one: a blocked candidate hands its chance
; to the next direction round rather than sharing it out, so at a corner the
; way after the blocked one comes up more often than the other. No test pins
; that share, and none should: it is a consequence of the scan, not a designed
; number.
; DESIGN: what IS pinned is the pair of properties the scan has to have, and
; both are tested. Every legal way out of a tile is reachable, which the two
; random bits are what buy: the outcome is the first legal direction at or
; after the start, so a start that ranges over all four is what lets all four
; be produced. And a centred ghost with a carry always moves, which is what
; the four tries and the step of one buy between them. Measured over two
; seeded windows of 600 frames: 0 stalls in 299 carries here, against 15 with
; three tries and 53 with a step of two, and at a four-way junction all three
; legal ways out appear here against one with the start folded to a single
; bit. An earlier round called those three changes distribution-only, on a
; measurement of direction share summed over frames, which is the one
; statistic all three preserve.
; DESIGN: the reversal is excluded here exactly as it is in pickdir, so a
; frightened ghost still never turns back on itself in a corridor. What does
; turn it round is the pill's one forced reversal, which is spent before the
; first random choice is ever made.
; DESIGN: IN sets no flags, so nothing here branches on the read itself. It
; is folded to two bits and stored, and the first branch below is the JZ on
; the SUB that tests for the reversal, which sets its own flags. Reading a
; port and branching straight off it is the bug the invaders demo shipped
; with, and the repository's own gotchas name it.
; DESIGN: canmove at its top entry, with the direction in A, and not cantile.
; pickdir enters at cantile because it steps the candidate tile itself and
; then hands that same tile to dist2; there is nothing to measure here, so the
; plain entry is the right one and the tile is stepped in one place.
; DESIGN: four tries and not three, and the step is one and not two. One of
; the four is the reversal, refused without asking the maze, so four tries at
; a step of one is exactly what offers the other three whichever direction the
; scan starts on. Either change leaves a ghost that reaches none of them,
; keeps its old direction into a wall, and stands still for the tick; see the
; stall figures in the note above.
frdir:  IN GPU_RAND -> A
        AND A <- 3           ; two bits: a direction, 0 to 3
        LD [frcand] <- A
        LD A <- 4
        LD [frn] <- A
frtry:  LD A <- [frcand]
        XOR A <- 2           ; a reversal is the direction exclusive-or 2
        CMP A, [adir]
        JZ frskip
        LD A <- [frcand]
        JSR canmove
        JZ frskip
        LD A <- [frcand]
        LD [adir] <- A
        RET
frskip: LD A <- [frcand]
        ADD A <- 1
        AND A <- 3
        LD [frcand] <- A
        LD A <- [frn]
        SUB A <- 1
        LD [frn] <- A
        JNZ frtry
; nothing open but the way it came: keep the direction, which is pickdir's own
; fallback and stands on the same ground. A ghost reaches it only in a dead
; end, and every dead end on the four shipped mazes is a tunnel mouth, which
; wrap moves the ghost off a moment later.
frnone: RET

advance: LD D1 <- dxtab
        LD A <- [adir]
        LD A <- [D1+A]
        LD [t3] <- A
        LD A <- [ax]
        ADD A <- [t3]
        LD [ax] <- A
        LD D1 <- dytab
        LD A <- [adir]
        LD A <- [D1+A]
        LD [t3] <- A
        LD A <- [ay]
        ADD A <- [t3]
        LD [ay] <- A
        RET

; --- is the windowed actor standing on a tunnel mouth? Answers nonzero when
; it is, and leaves that mouth's own way out in tunmd and the pair's other
; mouth in tunpx, tunpy and tunpd.
; DESIGN: OVERRIDE. A position, not an edge. The edge test this replaced asked
; "has the actor walked out of the 0..27 grid", which Metro Station's second
; pair never does: its mouths are columns 1 and 26, ordinary in-range tiles.
; DESIGN: free to use D1 and D2. Both callers -- wrap below and cantile's
; cmedge -- hold nothing in either. canmove loads D1 fresh for each of its
; two table lookups and is done with it by the time cantile is reached, and
; pdtry, which enters at cantile, does the same.
; DESIGN: two pointers, not an index and a multiply. D1 walks tuntab four
; bytes a slot and D2 walks tundtb two, side by side, so neither table needs
; a slot number worked out for it. There is no ALU operand behind a D
; register on this machine, so each byte compared costs a load into tunt
; first; the x halves are compared before the y halves so a miss, which is
; nearly every call, costs one pair of them and not two.
tunfind: LD A <- [tuncount]
        JZ tfno              ; no tunnel in this maze
        LD [tunn] <- A
        LD D1 <- tuntab
        LD D2 <- tundtb
tfloop: LD A <- [D1]
        LD [tunt] <- A
        LD A <- [ax]
        CMP A, [tunt]
        JNZ tfb
tfaxy:  LD A <- [D1+1]
        LD [tunt] <- A
        LD A <- [ay]
        CMP A, [tunt]
        JZ tfhita
tfb:    LD A <- [D1+2]
        LD [tunt] <- A
        LD A <- [ax]
        CMP A, [tunt]
        JNZ tfnext
tfbxy:  LD A <- [D1+3]
        LD [tunt] <- A
        LD A <- [ay]
        CMP A, [tunt]
        JZ tfhitb
tfnext: LD D1 <- D1+4
        LD D2 <- D2+2
        LD A <- [tunn]
        SUB A <- 1
        LD [tunn] <- A
        JNZ tfloop
tfno:   LD A <- 0
        RET
tfhita: LD A <- [D2]         ; on the a mouth, so b is the far one
        LD [tunmd] <- A
        LD A <- [D2+1]
        LD [tunpd] <- A
        LD A <- [D1+2]
        LD [tunpx] <- A
        LD A <- [D1+3]
        LD [tunpy] <- A
        LD A <- 1
        RET
tfhitb: LD A <- [D2+1]       ; on the b mouth, so a is the far one
        LD [tunmd] <- A
        LD A <- [D2]
        LD [tunpd] <- A
        LD A <- [D1]
        LD [tunpx] <- A
        LD A <- [D1+1]
        LD [tunpy] <- A
        LD A <- 1
        RET

; --- is the windowed actor inside a tunnel corridor? Answers nonzero when it
; is. It says nothing about which pair or which mouth: the one caller, asacc,
; only wants to know whether to slow down.
; DESIGN: a zone, not the mouth tunfind matches. tunfind asks "is this actor
; standing exactly on a mouth", because a warp happens at one position. A
; slowdown is a stretch of corridor, so this asks a different question and
; keeps its own scratch. Sharing tunfind's would be a trap: cantile calls it
; from inside canmove, which runs on the same tick as this does.
; DESIGN: the corridor's own axis, taken from the mouth's way out. A mouth is
; a position and nothing in tuntab says which way its corridor runs, so the
; direction table beside it is what says whether to widen x or y. The other
; axis has to match the mouth exactly. Widening both would make the zone a
; square 5 tiles either way, which at CLASSIC's row 14 would reach the
; long side corridors and slow a ghost nowhere near a tunnel.
; DESIGN: free to use D1 and D2, and it walks both. asacc's callers hold
; nothing in either, and what runs after it loads D1 fresh. Same ground as
; tunfind's own note, and for the same reason.
intunnel: LD A <- [tuncount]
        JZ itno              ; no tunnel in this maze
        LD [itn] <- A
        LD D1 <- tuntab
        LD D2 <- tundtb
itloop: LD A <- [D1]
        LD [itmx] <- A
        LD A <- [D1+1]
        LD [itmy] <- A
        LD A <- [D2]
        LD [itmd] <- A
        JSR itone
        JZ itmb
        RET                  ; near the a mouth: the answer is already in A
itmb:   LD A <- [D1+2]
        LD [itmx] <- A
        LD A <- [D1+3]
        LD [itmy] <- A
        LD A <- [D2+1]
        LD [itmd] <- A
        JSR itone
        JZ itnext
        RET
itnext: LD D1 <- D1+4
        LD D2 <- D2+2
        LD A <- [itn]
        SUB A <- 1
        LD [itn] <- A
        JNZ itloop
itno:   LD A <- 0
        RET

; --- is the actor inside the corridor of the one mouth in itmx, itmy, itmd?
; Even directions run sideways and odd ones up and down, which is what the
; low bit of the direction picks out.
itone:  LD A <- [itmd]
        TST A, 1
        JZ ithorz
; up or down: the column has to match, the row is the one with room in it.
        LD A <- [itmx]
        LD [itt] <- A
        LD A <- [ax]
        CMP A, [itt]
        JZ itvy
        LD A <- 0
        RET
itvy:   LD A <- [itmy]
        LD [itt] <- A
        LD A <- [ay]
        JMP itnear           ; a tail call: its answer is this one's
ithorz: LD A <- [itmy]
        LD [itt] <- A
        LD A <- [ay]
        CMP A, [itt]
        JZ ithx
        LD A <- 0
        RET
ithx:   LD A <- [itmx]
        LD [itt] <- A
        LD A <- [ax]
        JMP itnear

; --- is A within 40 of [itt]? Answers nonzero when it is.
; The magnitude first. A coordinate either side of the mouth is the same
; distance from it and SUB gives back only one of the two, the other having
; borrowed and wrapped. There is no negate, so the wrapped difference is
; parked and taken off zero.
itnear: SUB A <- [itt]
        JNC ittest
itneg:  LD [itt] <- A
        LD A <- 0
        SUB A <- [itt]
ittest: CMP A, 41
        JC ityes             ; borrow: the distance is 40 or less
        LD A <- 0
        RET
ityes:  LD A <- 1
        RET

; --- the tunnel. Asked once the actor has decided and before canmove is,
; and answers nonzero when it moved the actor, which spends the tick.
; An actor standing on a mouth and heading anywhere but back up that mouth's
; own corridor comes out at the far mouth of the pair, one tile along the far
; mouth's corridor, facing along it.
; DESIGN: OVERRIDE. It runs before canmove now, not after. Metro Station's
; second pair is a pocket at the bottom of a vertical corridor with wall on
; the other three sides, so the step that carries an actor into the tunnel is
; a step canmove refuses; asking canmove first meant the answer was "you
; cannot move" and the actor stood there. Running first also means canmove
; never has to answer from a mouth in the ordinary case.
; DESIGN: one tile past the far mouth, not onto it. Onto it would leave the
; actor standing on a teleporter, which is a state nothing else in the game
; produces and every rule here then has to be right about. It does NOT bounce
; back and forth: measured, landing on the far mouth instead turns 2 of the 82
; tests red and no more, because the guard below declines the very next tick,
; adir being the way out of the mouth it now stands on. One tile along that way
; lands it centred on a tile the maze has open, which mouthdir read off the
; maze when the mouth was recorded, and leaves nothing for the next tick to
; have to be right about.
; DESIGN: heading back up the corridor is not a warp. That is the actor
; turning round at the mouth and walking back into the maze, which the maze
; allows and which canmove below is what should answer. tunmd is the way out
; of the mouth being stood on, so the test is one compare.
wrap:   JSR tunfind
        JZ wrno              ; not standing on a mouth at all
        LD A <- [adir]
        CMP A, [tunmd]
        JZ wrno              ; heading back up this mouth's own corridor
        LD A <- [tunpd]
        LD [adir] <- A
        LD D1 <- dx8tab
        LD A <- [tunpd]
        LD A <- [D1+A]
        ADD A <- [tunpx]
        LD [ax] <- A
        LD D1 <- dy8tab
        LD A <- [tunpd]
        LD A <- [D1+A]
        ADD A <- [tunpy]
        LD [ay] <- A
        LD A <- 1
        RET
wrno:   LD A <- 0
        RET

; --- the pad. Bits match the BTN_ constants. IN sets no flags, so the
; value is tested with AND before any JZ.
readpad: IN IO_CONTROLLER -> A
        LD [t3] <- A
        TST A, BTN_LEFT
        JZ rpr
        LD A <- 2
        LD [pacnext] <- A
        RET
rpr:    LD A <- [t3]
        TST A, BTN_RIGHT
        JZ rpu
        LD A <- 0
        LD [pacnext] <- A
        RET
rpu:    LD A <- [t3]
        TST A, BTN_UP
        JZ rpd
        LD A <- 3
        LD [pacnext] <- A
        RET
rpd:    LD A <- [t3]
        AND A <- BTN_DOWN
        JZ rpx
        LD A <- 1
        LD [pacnext] <- A
rpx:    RET

; --- move the windowed actor's sprite to where the window says it is. The
; only caller is the actor walk above, which loads a record into the window
; and sets acti before every call.
; DESIGN: the sprite id is acti plus one, so actor 0 is sprite 1 and the four
; ghosts are 2 to 5. The plus one is Pac-Man's sprite number from stage 1,
; carried forward rather than renumbered. Slot 0 staying empty suits what comes
; next as well: CMD_COLLIDE_ALL fills a table whose zero entry means "nothing",
; so a game that reads it wants no sprite in slot 0.
; DESIGN: the frame comes from a different place for each kind of actor.
; Pac-Man's strip is twelve frames, four facings times three mouth positions,
; so setframe computes dir times 3 plus phase and writes the port itself. A
; ghost's strip is eight frames, two per facing, so its frame is the
; direction times two plus the skirt gwig gives.
; DESIGN: the draw owns the ART as well as the frame, through adstrip below.
; The state byte says what a ghost IS and the strip says what a player SEES,
; and they are two facts that must never disagree. They were kept in step by
; hand at five separate sites and placeghosts, a sixth, wrote states and no
; art at all: an eaten ghost caught by a life-loss reset came back normal
; still wearing the eyes, so it was lethal and drawn as the one thing on the
; board that cannot touch you. The owner met it in a real game. Nothing writes
; a strip at runtime any more except the routine that draws.
; DESIGN: an eaten ghost needs no arm of its own here, and that is why the
; eyes strip carries eight frames, each facing twice, rather than one. State 3
; falls past the state 2 test onto the direction path above, so a pair of
; eyes faces the way it is travelling for free. A one frame strip would have
; worked too and would have cost this routine a third branch.
; DESIGN: the sprite id is written again before every command, because the
; data ports clear behind each one. It is kept in aspr rather than recomputed,
; so the three commands below cannot disagree about which sprite they mean.
actdraw: LD A <- [acti]
        ADD A <- 1
        LD [aspr] <- A
        JSR adstrip
        LD A <- [acti]
        JZ adpacf
        LD A <- [astate]
        CMP A, 2
        JZ adfrgh
        LD A <- [adir]
        SHL A                ; two frames a facing
        LD [adfr] <- A
        JSR gwig
        ADD A <- [adfr]
        JSR adframe
        JMP adxy
; --- a frightened ghost's frame comes from the clock, not from its facing.
; DESIGN: writing the facing here would be wrong and not merely pointless.
; The frightened strip is four frames and a facing's frame is 0 to 7, and
; the GPU takes a frame number modulo the strip's own length. Facing down or
; up would land on 2 to 3 or 6 to 7, the white flash, for as long as the
; ghost faced that way. The eight frame strips every other ghost uses are
; unaffected, 0 to 7 modulo 8 being the identity.
; DESIGN: it alternates once fewer than PACMAN_FLASH_UNITS units are left, on
; bit 0 of the countdown, so the flash costs one AND. The compare is a strict
; "less than", so the flashing span is one unit shorter than the constant. One
; clock drives all four, so they flash on the same frame, which is what makes
; it read as a warning rather than as four ghosts blinking.
; The strip is blue in frames 0 and 1 and white in 2 and 3, and the skirt
; picks between the two of a colour.
adfrgh: LD A <- [frtk]
        CMP A, 15
        JC adflash           ; borrow: inside the last units of the window
        JSR gwig             ; blue, and the skirt
        JSR adframe
        JMP adxy
adflash: LD A <- [frtk]
        AND A <- 1
        SHL A                ; 0 blue, 2 white
        LD [adfr] <- A
        JSR gwig
        ADD A <- [adfr]
        JSR adframe
        JMP adxy

; --- the skirt: A := 0 or 1, turning over every eight frames of the GPU
; clock. One clock for every ghost, so the four ripple together, as the
; arcade's do.
gwig:   LD A <- [mainfr]
        SHR A
        SHR A
        SHR A
        AND A <- 1
        RET
adpacf: JSR setframe
adxy:   LD A <- [aspr]
        OUTA GPU_SPRITE
        OUT GPU_SPRITE_X_HI, 0
        OUT GPU_SPRITE_Y_HI, 0
; ax and ay are maze pixels. Each offset goes on before the centring. A
; sprite on tile column 0 then starts two pixels into the left margin, and
; one on tile row 0 two pixels into the strip, so neither reaches adcent's
; floor on a shipped maze. The floor stays for a maze that fills the width,
; where mzxleft is 0; see adcent.
        LD A <- [ax]
        ADD A <- [mzxleft]
        JSR adcent
        OUTA GPU_SPRITE_X
        LD A <- [ay]
        ADD A <- [mzytop]
        JSR adcent
        OUTA GPU_SPRITE_Y
        OUT GPU_CMD, CMD_SPRITE_MOVE
        LD A <- [aspr]
        CMP A, 1
        JZ adtop
        RET
; Pac-Man: his upper copy goes to the same place, and paclayer decides
; whether it shows. The ports cleared behind the move, so all four go again.
adtop:  OUT GPU_SPRITE, PACTOP
        OUT GPU_SPRITE_X_HI, 0
        OUT GPU_SPRITE_Y_HI, 0
        LD A <- [ax]
        ADD A <- [mzxleft]
        JSR adcent
        OUTA GPU_SPRITE_X
        LD A <- [ay]
        ADD A <- [mzytop]
        JSR adcent
        OUTA GPU_SPRITE_Y
        OUT GPU_CMD, CMD_SPRITE_MOVE
        JMP paclayer

; Show the frame in A on this actor's sprite. The sprite is named again
; because the data ports clear behind every command. Pac-Man's frame goes to
; his upper copy too.
adframe: LD [gs0] <- A
        LD A <- [aspr]
        OUTA GPU_SPRITE
        LD A <- [gs0]
        OUTA GPU_SPRITE_FRAME
        OUT GPU_CMD, CMD_SPRITE_FRAME
        LD A <- [aspr]
        CMP A, 1
        JZ adftop
        RET
adftop: OUT GPU_SPRITE, PACTOP
        LD A <- [gs0]
        OUTA GPU_SPRITE_FRAME
        OUT GPU_CMD, CMD_SPRITE_FRAME
        RET

; --- which Pac-Man is seen: the one under the ghosts or the one over them.
; The upper copy shows unless a hunting ghost, in the house or out of it,
; overlaps his box. Then the ghost is drawn over him, as it catches him. A
; frightened ghost or a pair of eyes is drawn under him, as he eats it.
; DESIGN: the test is CMD_HIT_TEST on the sprites as they stand, so a ghost
; drawn later in this walk is measured where it was last frame. One frame
; behind costs nothing a player can see, and asking before the walk would
; cost a second walk.
; DESIGN: D1 walks the ghost records for their state bytes. The actor walk
; reloads its own pointer from acur after every draw, so nothing here needs
; to give D1 back.
paclayer: LD A <- [pacvis]
        JZ plhide
        LD D1 <- blinky
        LD A <- 2
        LD [plspr] <- A
plloop: LD A <- [D1+5]
        CMP A, 2
        JNC plnext           ; 2 frightened or 3 eyes: he may cover it
        OUT GPU_SPRITE, 1
        LD A <- [plspr]
        OUTA GPU_SPRITE_B
        OUT GPU_CMD, CMD_HIT_TEST
        IN GPU_HIT -> A
        OR A <- 0            ; IN sets no flags
        JNZ plhide           ; a hunting ghost overlaps him: it stays in front
plnext: LD D1 <- D1+8
        LD A <- [plspr]
        INC A
        LD [plspr] <- A
        SUB A <- 6
        JNZ plloop
        OUT GPU_SPRITE, PACTOP
        OUT GPU_CMD, CMD_SPRITE_SHOW
        RET
plhide: OUT GPU_SPRITE, PACTOP
        OUT GPU_CMD, CMD_SPRITE_HIDE
        RET

; --- put the strip this actor's state calls for onto its sprite, and only
; when the answer changes.
; DESIGN: called from actdraw, after the sprite is selected and before the
; frame is chosen, because CMD_SPRITE_DEF applies to the sprite named in DATA0 and
; resets its frame to 0.
; DESIGN: astrip caches what each sprite is wearing, so a state that has not
; changed costs a load, a compare and a return. Without it every frame would
; redefine five sprites, and a define copies the whole strip on the GPU side
; and marks the screen dirty. The same shape as bedcur in sndbed, and for the
; same reason: the answer is cheap to work out and expensive to apply.
; DESIGN: the cache ships as 0, which is not a strip any actor can want, so
; the first draw after a load or a Restart always defines. A Reset keeps both
; RAM and the GPU, so the cache and the screen stay in step across one.
; DESIGN: Pac-Man takes the first arm and never looks at his state byte. His
; record carries 1 there and nothing ever changes it, but reading it would say
; that a Pac-Man in state 2 should turn blue, which is not a rule this game
; has.
adstrip: LD A <- [acti]
        JZ adskp             ; Pac-Man: one strip, whatever his state says
        LD A <- [astate]
        CMP A, 2
        JZ adskf
        CMP A, 3
        JZ adske
        LD A <- [acti]       ; in the house or hunting: its own colours
        JMP adswant
adskp:  LD A <- 5
        JMP adswant
adskf:  LD A <- 6
        JMP adswant
adske:  LD A <- 7
adswant: LD [adsk] <- A
        JSR strptr           ; D1 := what this sprite is wearing
        LD A <- [D1]
        SUB A <- [adsk]
        JZ adsdone           ; already wearing it: nothing to do
        LD A <- [adsk]
        LD [D1] <- A
        CMP A, 5
        JZ adslp
        LD A <- [adsk]
        CMP A, 6
        JZ adslf
        CMP A, 7
        JZ adsle
        JMP ghstrip          ; a tail call: it points the latches and defines
adslp:  LD A <- [aspr]
        OUTA GPU_SPRITE
        OUT GPU_SRC_BANK, get_bankbyte(pacspr)
        OUT GPU_SRC_HI, get_highbyte(pacspr)
        OUT GPU_SRC_LO, get_lowbyte(pacspr)
        JMP adsdef
adslf:  LD A <- [aspr]
        OUTA GPU_SPRITE
        OUT GPU_SRC_BANK, get_bankbyte(frightspr)
        OUT GPU_SRC_HI, get_highbyte(frightspr)
        OUT GPU_SRC_LO, get_lowbyte(frightspr)
        JMP adsdef
adsle:  LD A <- [aspr]
        OUTA GPU_SPRITE
        OUT GPU_SRC_BANK, get_bankbyte(eyesspr)
        OUT GPU_SRC_HI, get_highbyte(eyesspr)
        OUT GPU_SRC_LO, get_lowbyte(eyesspr)
adsdef: OUT GPU_CMD, CMD_SPRITE_DEF
adsdone: RET

; --- centre one coordinate on the actor's tile: A minus 3, floored at 0.
; DESIGN: a record holds a tile origin and a tile is 8 pixels, so a 14 pixel
; sprite centred on that tile starts three pixels earlier on each axis.
; DESIGN: floored, not wrapped, and the floor is not decoration. CLASSIC's
; tunnel mouths are row 14 columns 0 and 27, so an actor walking onto the left
; one holds ax 0. With the maze against the screen's edge that is 0 minus 3 in
; a byte, which is 253. Flooring costs that tile a three pixel offset, which
; nobody can see; wrapping would throw the sprite to the far side of the
; screen for every frame spent there, which everybody can. The GPU does not
; save us either: CMD_SPRITE_MOVE takes the data byte as an unsigned pixel
; column, so 253 is a real place and not an error.
; Both coordinates arrive with their offset already added. Tile row 0 sits at
; screen row 8 and centring takes it to 5; tile column 0 sits at screen column
; mzxleft, 16 on every shipped maze, and centring takes it to 13. So today
; neither axis reaches the floor. It stays for a maze 32 columns wide, whose
; mzxleft is 0 and whose left mouth is the case above. One routine for both
; axes costs nothing.
; The considered alternative was a true negative: GPU_X_HI $FF with GPU_X $FD
; reads as -3 (the GPU sign-extends the 16 bit pair), which would keep the
; sprite exactly centred and let it clip off the edge. Rejected because it
; hides three columns of an actor who is fully on screen everywhere else, and
; because it costs adxy a second conditional per axis for the one tile in the
; maze that can reach it.
adcent: SUB A <- 3
        JC adedge
        RET
adedge: LD A <- 0
        RET

; --- pick the strip frame from direction and mouth phase.
; DESIGN: frame is dir times 3 plus phase, and there is no multiply, so the
; three is three adds. The phase walks 0,1,2,1 rather than 0,1,2,0 so the
; mouth closes rather than snapping shut.
setframe: LD A <- [pactk]
        JZ sfstep
        SUB A <- 1
        LD [pactk] <- A
        JMP sfdraw
; DESIGN: 4 here, not the step count, and not the 3 this held before. Setting
; pactk to n and counting it down means a phase step lands every n + 1
; setframe calls, this call plus n countdown calls. What changed under it is
; how often setframe is called: it used to run from the step, only on an aacc
; carry tick, and now runs from the once-a-frame actor walk, on every frame.
; 3 with the old caller measured 5.11 real frames a phase, irregular because a
; tick with no carry repeated one; 3 with the new caller measures 4.13, a
; quarter faster, and 4 measures 5.19 and is regular. Chosen to keep the mouth
; chewing at the speed it chewed at before the draw moved.
sfstep: LD A <- 4
        LD [pactk] <- A
        LD A <- [pacpd]
        JZ sfopen
        LD A <- [pacph]        ; closing
        SUB A <- 1
        LD [pacph] <- A
        JZ sfturn
        JMP sfdraw
sfopen: LD A <- [pacph]
        ADD A <- 1
        LD [pacph] <- A
        CMP A, 2
        JNZ sfdraw
sfturn: LD A <- [pacpd]        ; hit an end: swap direction
        CMP A, 1
        JZ sfopening
        LD A <- 1
        LD [pacpd] <- A
        JMP sfdraw
sfopening: LD A <- 0
        LD [pacpd] <- A
sfdraw: LD A <- [adir]         ; frame = dir * 3 + phase
        SHL A
        ADD A <- [adir]
        ADD A <- [pacph]
        LD [pacfrm] <- A
        JMP adframe            ; a tail call: it names the sprite and shows it

; =========================== eating and levels ===========================
; --- eat whatever is under him. Writes a space, so the same tile never
; scores twice, and paints the cell out at the background colour.
eat:    JSR pactile
        JSR tileat
        LD [dch] <- A
        CMP A, $2E         ; '.'
        JZ eatdot
        LD A <- [dch]
        SUB A <- $6F         ; 'o'
        JZ eatpill
        RET
; A mouthful costs him a frame, and a pill three. Written here rather than in
; eatdo because the two costs differ, and read by asacc on the frames after
; this one: eat runs from pacdecide, which is already past this tick's
; accumulator, so the pause starts on the next frame and not on this one.
eatdot: JSR sndwaka          ; the chomp, on a dot and not on a pill
        LD A <- 1
        LD [pacstl] <- A
        LD A <- 1            ; 10 points, held in tens
        JMP eatdo
; a pill is a dot that scores five times as much and turns the hunt around.
; frighten leaves A alone as far as this is concerned: the score is loaded
; after it returns, not before.
eatpill: JSR frighten
        LD A <- 3
        LD [pacstl] <- A
        LD A <- 5           ; 50 points
eatdo:  LD [t3] <- A
        JSR addscore
; cellrect paints at (px, py), which drawmaze set as it walked and has long
; since moved on. He is centred here (eat only runs from pacdecide, itself
; reached only from actstep's centred branch), so the window's ax and ay are
; themselves tile-aligned: set px, py from them directly rather than
; recomputing a tile origin. The window, not his record: the record still
; holds where he was when the walk loaded it, and saveact has not run yet.
        LD A <- [ax]
        LD [px] <- A
        LD A <- [ay]
        LD [py] <- A
        JSR tileptr
        LD A <- $20
        LD [D1] <- A
        OUT GPU_COLOR, 0
        OUT GPU_CMD, CMD_SET_COLOR
        JSR cellrect         ; paint the cell out
        JSR dropdot
        RET

; lower the 16-bit count by one. Reaching zero clears the level.
dropdot: LD D1 <- dots
        LD A <- [D1+1]
        SUB A <- 1
        LD [D1+1] <- A
        JNC droptest
dropahi: LD A <- [D1]
        SUB A <- 1
        LD [D1] <- A
droptest: LD A <- [D1]
        JZ droplo
        RET
droplo: LD A <- [D1+1]
        JZ nextlvl
        RET

; add t3 tens of points to the 16-bit score, and show it.
addscore: LD D1 <- score
        LD A <- [D1+1]
        ADD A <- [t3]
        LD [D1+1] <- A
        JNC hud   ; a tail call: hud's RET answers addscore's caller
ashi:   LD A <- [D1]
        ADD A <- 1
        LD [D1] <- A
        JMP hud

; --- print the score strip: the score, the lives and the level, in the top
; text row, which is the eight screen rows the maze leaves free.
; DESIGN: called when a value changes and never from the frame loop. The
; three writers are addscore, dieover and nextlvl, so a frame that changes
; nothing costs no printf. A dot is the frequent case, one print every few
; frames, and a print is a dozen OUTs.
; DESIGN: the arguments are the game's own bytes. score, lives and lvlno are
; declared together, in the template's order, and printf reads them where
; they are. The strip therefore cannot disagree with the game.
; DESIGN: the cursor is set on every print. Drawing advances it, and a
; second print from where the first ended would walk down the screen.
; DESIGN: the score is held in tens, so the template prints it with a
; trailing 0 and pads to four digits, which is SCORE 00000 at the start.
; Above 9999 tens the field widens by one column. The gaps are sized so the
; longest strip, a five digit score on a three digit level, fits 42 columns.
hud:    OUT GPU_TEXT_COL, 0
        OUT GPU_TEXT_ROW, 0
        OUT GPU_CMD, CMD_TEXT_AT
        OUT GPU_CART_BANK, get_bankbyte(hudfmt)
        OUT GPU_CART_HI, get_highbyte(hudfmt)
        OUT GPU_CART_LO, get_lowbyte(hudfmt)
        OUT GPU_TEXT_ARG_HI, score >> 8
        OUT GPU_TEXT_ARG_LO, score & 255
        OUT GPU_CMD, CMD_PRINTF
        RET

; --- a cleared board fades out, loads the next maze, and fades back in.
; DESIGN: the fade hides the draw, so every level starts the same way and
; nobody ever watches the maze being painted.
; DESIGN: droplo above branches here with JZ, not JSR, so nextlvl is a
; tail call. It pushes no return address, and its RET spends the one
; JSR dropdot pushed, landing back in eat just after that call.
nextlvl: JSR sndoff          ; the board is over: silence across the fade
        JSR fadeout
        LD A <- [level]
        INC A
        LD [level] <- A
        CMP A, 5
        JC nlkeep
        LD A <- 1            ; four mazes, then round again
        LD [level] <- A
nlkeep: LD A <- [lvlno]      ; the level number does not round: see lvlno
        INC A
        LD [lvlno] <- A
        JSR hud
; drawmaze resets every actor's record: Pac-Man's position from the new maze's
; own P via dmpac, his direction and accumulator via the per-level block, and
; all four ghosts via placeghosts. This whole path runs inside some actor's
; step, so the window still holds where that actor was when the last dot fell,
; and the walk's saveact would write it straight back over the reset on the
; way out.
; DESIGN: which actor is mid-step is remembered and handed back, rather than
; assumed to be Pac-Man. He is the only actor that can reach here today, since
; only he eats. All THREE of the walk's scalars are saved, because drawall
; below runs the same walk from inside this one and leaves all three as its
; own nested pass left them:
;   acur, the record, ends on Clyde, so the tail of the interrupted step is
;     written into Clyde's record instead of the one it belongs to;
;   acti, the counter, ends on 5, so the resumed walk's exact-equality test
;     (SUB A <- 5 / JZ aadone) misses on 6 and the counter wraps the whole
;     byte. Traced: 255 further passes, 250 of them with the counter past the
;     end of the array, marching D1 from 61 to 2093 -- two thousand bytes past
;     the records, through work, rowtab, the direction tables and mazetab,
;     loading eight bytes and writing six back at every one;
;   aastep, step or draw only, ends on 0, so every actor after the interrupted
;     one is drawn but neither stepped nor saved for the rest of that frame.
; The walk itself is what the test watches, because that is where all three
; failures are: what the walk does, not what it leaves behind. See the aastep
; note at actall.
        LD A <- [acti]
        LD [nlacti] <- A
        LD A <- [aastep]
        LD [nlastep] <- A
        LD D1 <- [acur]
        LD [nlacur] <- D1
; The difficulty row moves before the draw, not after it: drawmaze reads it
; for Pac-Man's speed through lvlspd, placeghosts for the ghosts', and msdur
; for the new level's scatter timer.
        JSR lvlstep
        JSR loadmaze
        JSR drawmaze
; Nothing told the sprites to follow until the walk ran again after the whole
; fade finished. For those 30 frames every actor sat on the old maze's tile
; while the new one rose around it, then jumped. Drawing here, before the
; fade-in, keeps all five in place from the first visible frame of the new
; maze. drawall only draws: the step in progress is this one, and running it
; again would move an actor twice on the tick the level changed.
        JSR drawall
        LD A <- [nlacti]
        LD [acti] <- A
        LD A <- [nlastep]
        LD [aastep] <- A
        LD D1 <- [nlacur]
        JSR loadact
        JSR fadein
        RET

; --- fade the three maze entries back to black over 30 frames.
; DESIGN: OVERRIDE. Drives fwb and fdb, not a single new counter: fput
; below always reads those two names for the wall's blue and the dot and
; pill blue, so a fade that wrote anywhere else would leave both frozen at
; their fade-in values until the hard snap at the end, and the ramp would
; never visibly darken.
fadeout: LD A <- 0
        LD [fstep] <- A
        LD A <- 255
        LD [fwb] <- A
        LD A <- 255
        LD [fr] <- A
        LD A <- 224
        LD [fg] <- A
        LD A <- 176
        LD [fdb] <- A
fowait: IN GPU_FRAME
        CMP A, [fframe]
        JZ fowait
        LD [fframe] <- A          ; CMP kept the frame in A
        LD A <- [fwb]
        SUB A <- 8
        LD [fwb] <- A
        LD A <- [fr]
        SUB A <- 8
        LD [fr] <- A
        LD A <- [fg]
        SUB A <- 7
        LD [fg] <- A
        LD A <- [fdb]
        SUB A <- 5
        LD [fdb] <- A
        JSR fput
        LD A <- [fstep]
        INC A
        LD [fstep] <- A
        SUB A <- 30
        JNZ fowait
foblack: LD D1 <- palbuf+3
        LD A <- 0
        LD [D1]+ <- A
        LD [D1]+ <- A
        LD [D1]+ <- A
        LD [D1]+ <- A
        LD [D1]+ <- A
        LD [D1]+ <- A
        LD [D1]+ <- A
        LD [D1]+ <- A
        LD [D1] <- A
        LD D1 <- palbuf+WALLDIM*3
        LD [D1]+ <- A
        LD [D1]+ <- A
        LD [D1]+ <- A
        LD [D1]+ <- A
        LD [D1]+ <- A
        LD [D1] <- A
        OUT GPU_CMD, CMD_FETCH_PALETTE
        RET

; =========================== contact and lives ===========================
; --- has a ghost caught him? Broad phase on the GPU, narrow phase here.
; DESIGN: a bounding box is not a catch. Two 14 by 14 sprites overlap while
; their centres are thirteen pixels apart, which is a tile and a half, and a
; player watching that would call it a miss. A box test therefore kills
; Pac-Man early and feels unfair. So the box is read as "look closer" and
; what decides is the distance between the two actors' centres.
; DESIGN: the broad phase is an OPTIMISATION, not a rule. CMD_HIT_SCAN asks
; the GPU whether Pac-Man's box overlaps any other sprite at all, which is one
; command and one read whatever the answer, and on most frames the answer ends
; the routine. Deleting it and measuring all four ghosts every frame changes
; no outcome at all, only the cost: measured, all 106 tests stay green with
; the scan taken out. Nothing here may come to depend on it.
; DESIGN: the scan's answer is read as a yes or a no and never as "which
; ghost". It reports the LOWEST sprite the box overlaps, and two ghosts can
; overlap him at once with the nearer one the higher numbered, so the narrow
; phase measures all four rather than the one the scan named.
; DESIGN: 255 is the GPU's "nothing overlaps", not 0. See firstHit in gpu.ts,
; which is handed 0xff as its "none" for CMD_HIT_SCAN. Sprite 0 is never
; defined here, so a ghost is 2 to 5. Sprite 6, PACTOP, is Pac-Man's own
; upper copy and overlaps him whenever it shows. The scan answers the lowest
; sprite, so a ghost wins over the copy, and anything from 6 up means no
; ghost is near.
; DESIGN: IN sets no flags on this machine, so the CMP below is what tests
; the port read. A branch straight after the IN would read whatever the last
; ALU operation left, which is the stale-flag bug the invaders demo shipped
; with and the repository's own gotchas call out.
contact: OUT GPU_SPRITE, 1
        OUT GPU_CMD, CMD_HIT_SCAN
        IN GPU_HIT -> A
        CMP A, PACTOP
        JNC ctno             ; 255 or his own upper copy: no ghost is near
; --- narrow phase. Both sprites are the same size and both records hold a
; tile ORIGIN, so the offset from origin to centre is identical for the two of
; them and cancels: the distance between the origins IS the distance between
; the centres. There is no offset arithmetic below because none is needed.
; DESIGN: a pixel-level sibling of dist2 rather than dist2 itself. dist2
; measures between dcol, drow and tcol, trow, which are TILE coordinates its
; callers put there, pickdir through pactile and p2t. Tiles are the unit a
; ghost steers in and the wrong unit for a four pixel radius: every catch and
; every near miss in this routine happens inside one tile, where dist2 would
; answer 0 or 1 whatever the pixels say. Only sqtab is shared, and it is
; shared exactly as dist2 shares it: a leg doubled is the index, because the
; table holds words.
; DESIGN: dist2's own scratch is reused, d2leg and the two word pairs. Safe
; because contact runs from the frame loop, between walks, and dist2's answer
; is spent inside pickdir before pickdir returns, so nothing of dist2's is
; live at this point in a frame. That is a constraint on where contact may be
; called from, and the frame loop's DESIGN note above states the other half of
; it.
        LD A <- [pacx]
        LD [cpx] <- A
        LD A <- [pacy]
        LD [cpy] <- A
        LD A <- 4
        LD [cgn] <- A
        LD D1 <- blinky
        LD [cgp] <- D1
ctloop: LD D1 <- [cgp]
        LD A <- [D1]
        LD [cgx] <- A
        LD A <- [D1+1]
        LD [cgy] <- A
        LD A <- [D1+5]
        LD [cgs] <- A
; the column leg, as an absolute difference in pixels
        LD A <- [cgx]
        SUB A <- [cpx]
        JNC ctxabs            ; borrow: Pac-Man is the further right
ctxneg: LD A <- [cpx]
        SUB A <- [cgx]
; DESIGN: the clamp is live here, unlike dist2's row clamp. Every ghost is
; measured on a frame the scan fired, including the three that are nowhere
; near him, and a leg between two pixel coordinates reaches 255. 63 is the
; table's last entry and further away than any catch.
ctxabs: LD [d2leg] <- A
        CMP A, 64
        JC ctxsq
        LD A <- 63
        LD [d2leg] <- A
ctxsq:  LD A <- [d2leg]
        SHL A     ; twice the leg: the table holds words
        LD D1 <- sqtab
        LD D2 <- [D1+A]
        LD [d2hi] <- D2
; and the row leg, the same shape
        LD A <- [cgy]
        SUB A <- [cpy]
        JNC ctyabs
ctyneg: LD A <- [cpy]
        SUB A <- [cgy]
ctyabs: LD [d2leg] <- A
        CMP A, 64
        JC ctysq
        LD A <- 63
        LD [d2leg] <- A
ctysq:  LD A <- [d2leg]
        SHL A
        LD D1 <- sqtab
        LD D2 <- [D1+A]
        LD [d2shi] <- D2
; add the row square in, low byte first so the carry runs the right way. A
; byte load leaves C alone, which is what lets the ADC see the ADD's carry.
        LD A <- [d2lo]
        ADD A <- [d2slo]
        LD [d2lo] <- A
        LD A <- [d2hi]
        ADC A <- [d2shi]
        LD [d2hi] <- A
; --- inside the radius? The whole comparison fits one byte. A catch is at
; most HITRAD squared, which is 16 at a radius of 4, so any squared distance
; with a high byte at all is further away than the radius and the low byte
; never has to be looked at.
        LD A <- [d2hi]
        JNZ ctnext
cttest: LD A <- [d2lo]
        SUB A <- 17
        JNC ctnext           ; borrow: nearer than the radius, so a catch
; --- touching. What happens now is the ghost's state, and all three states a
; ghost out of the house can be in do something different. 1 is a normal ghost
; and he dies. 2 is frightened, and the ghost is eaten instead. 3 is a ghost
; already eaten, a pair of eyes on its way home, and it touches nobody.
; DESIGN: two compares and no third. Eyes are the fall-through, so nothing here
; spells them out, and a state byte that was none of the three would land on
; the same arm and do nothing, which is the safe answer to a question this
; routine cannot make sense of.
cttouch: LD A <- [cgs]
        SUB A <- 1
        JZ ctdie
        LD A <- [cgs]
        SUB A <- 2
        JZ cteat
        JMP ctnext
ctnext: LD D1 <- [cgp]
        LD D1 <- D1+8
        LD [cgp] <- D1
        LD A <- [cgn]
        SUB A <- 1
        LD [cgn] <- A
        JNZ ctloop
ctno:   RET
; caught. The board holds still for a moment and then comes back a life short.
ctdie:  JSR snddie
        LD A <- 2
        LD [gstate] <- A
        LD A <- 60
        LD [dietk] <- A
        RET

; --- a frightened ghost is caught. It scores, and what is left of it goes
; home as a pair of eyes.
; DESIGN: the loop carries on rather than returning, so every ghost touching
; him this frame is read this frame. Two blue ones are both eaten, the second
; at twice the first. The case that makes an early return WRONG rather than
; merely late is a blue ghost in front of a normal one: contact walks the
; records in order, so a RET here would spend the catch and never reach the
; ghost that kills him, and he would live a frame he should not have. Measured
; on the catch frame itself, blue on 2 and normal on 3: this build reads score
; 25 and gstate 2, the RET reads 25 and gstate 1.
; That note used to say the opposite -- that an early return cost a frame of
; latency and nothing else, because contact runs again next frame -- and cited
; a green suite as the evidence. The suite was green because nothing had built
; the scenario the sentence after it described. It is built now, in "still dies
; to a normal ghost standing behind a blue one", and the mutant dies on it.
; DESIGN: doubling is a shift the machine does not have, so the value is added
; to itself. The carry is what caps it: 160 plus 160 is 320, and a byte cannot
; carry that, so the chain holds at the top instead of wrapping to 64. A fifth
; catch inside one window is reachable, because a ghost that gets home while
; the pill is still standing walks back out blue.
; DESIGN: t3 is the argument addscore takes, and it is free here. contact runs
; from the frame loop between walks, and every routine that writes t3 --
; canmove, advance, houptr, the startup loop -- spends it inside one call.
; DESIGN: the record, not the actor window. contact runs between walks, so
; every record is settled and no actor is loaded; frighten writes records for
; the same reason. D1 has to be reloaded from cgp, because the two sqtab
; lookups above left it standing on the table.
; DESIGN: the sprite is 6 minus cgn. cgn counts the four ghosts DOWN from 4 to
; 1 and Blinky, the first, is sprite 2, so 6 minus the counter is the sprite
; without a second counter to keep in step with this one.
; DESIGN: one eyes strip for all four, like the frightened one. An eaten ghost
; has no colour left, so there is nothing to tell four strips apart, and the
; four frames it does carry are the four facings.
cteat:  JSR sndeat
        LD A <- [ghchain]
        LD [t3] <- A
        JSR addscore
        LD A <- [ghchain]
        SHL A
        JC cteheld           ; past a byte: the chain holds at the top
        LD [ghchain] <- A
cteheld: LD D1 <- [cgp]
        LD A <- 3            ; state: eaten, and on its way home as eyes
        LD [D1+5] <- A
        LD A <- 255
        LD [D1+4] <- A
        JMP ctnext

; --- the dying pause, one frame of it, from the frame loop.
; DESIGN: a pause and not an animation, which is what the spec asks for. The
; frame loop does not run the actor walk while gstate is 2, so nothing steps
; and no sprite is redrawn: all five hold the positions the frame of the catch
; left them in, which is the ghost sitting on top of him.
; DESIGN: the mode clock stops too, because modestep is on the playing arm.
; A mode is a length of play, and a second spent frozen with nobody hunting is
; not a second of the hunt. Running it here would also lose a flip: a mode
; change raised during the pause raises modefl, and resetact clears modefl,
; so the reversal that change owed would go unpaid and no ghost would turn.
diestep: LD A <- [dietk]
        JZ dieover
        SUB A <- 1
        LD [dietk] <- A
        RET
; the pause is over. One life goes, and at zero the game is over.
; DESIGN: the subtraction is the test. lives is counted down by one and JZ
; reads the result, so nothing compares against a constant and nothing has to
; agree with the 3 the byte ships with.
dieover: LD A <- [lives]
        SUB A <- 1
        LD [lives] <- A
        JSR hud              ; the strip shows the life go, at zero too
        LD A <- [lives]
        JZ dieall
        JSR resetact
        LD A <- 1
        LD [gstate] <- A
        RET
; DESIGN: nothing leaves state 3 from inside the game, so reaching it ends
; the game. The frame loop's game over arm leaves for the game over screen on
; the next arrival, and that screen's key leads round to the attract screen
; and a new game. This routine only sets the state. The screen change belongs
; to the dispatch, which is reached with no frame half finished.
dieall: LD A <- 3
        LD [gstate] <- A
        RET

; --- put all five actors back where this maze starts them, and leave the
; board exactly as it is. A death costs a life, not a dot.
; DESIGN: not a redraw. drawmaze would repaint from the working array, which
; still holds every eaten dot as a space, so the dots would survive either
; way; but it also costs about 139,500 instructions, the figure the draw's own
; tests carry, and re-reads a maze that has not changed. What has to move is the five records, so only they are written.
; DESIGN: Pac-Man's spawn comes from pacsx and pacsy, which dmpac wrote as the
; draw passed his P marker. His record's own first two bytes cannot serve:
; they hold where he is, which is where he was caught.
; DESIGN: free to run the walk, and nothing is saved and handed back. This is
; reached from the frame loop and not from inside a walk, unlike nextlvl,
; which is reached from inside an actor's own step and has to hand acti,
; aastep and acur back. contact runs after actall has returned; see the frame
; loop's note.
; DESIGN: the scatter and chase clock is NOT reset. It belongs to the level:
; drawmaze resets it when the maze changes, and a death changes neither the
; maze nor the dots. modefl is cleared, and only modefl, because a reversal
; owed to the life that just ended is not owed to the next one, and the walk
; that would have spent it never ran.
resetact: LD A <- [pacsx]
        LD [pacx] <- A
        LD A <- [pacsy]
        LD [pacy] <- A
        LD A <- 3            ; up, the direction his spawn tile blocks
        LD [pacdir] <- A
        LD [pacnext] <- A
        LD A <- 0
        LD [pacacc] <- A
        LD [modefl] <- A
; DESIGN: the fright IS reset, where the mode clock is not, and the two are
; not the same kind of thing. A mode belongs to the level and a death changes
; neither the maze nor the dots; a pill belongs to the moment, and the moment
; ended when he was caught. It also has to be: placeghosts below writes every
; ghost's state and speed but nothing about the strip it is wearing, so a
; ghost put back in the house while blue would stay blue for good.
        JSR unfright
        JSR lvlspd
        JSR placeghosts
        JSR drawall
        RET

; =========================== sound ===========================
; DESIGN: two kinds of sound, and they are two because the chip's voices are.
; The chomp and the two stings are ONE SHOTS: triggered and forgotten, each
; ends itself when it runs off the end of its sample. The bed -- the siren, the
; fright warble and the eyes -- is a LOOPING voice, and a looping voice never
; ends on its own. So exactly one routine owns it, sndbed, and exactly one bed
; voice is ever sounding.
; DESIGN: three tracks, because a track holds eight players and steals its own
; oldest when a ninth is asked for. Alone on a track, the bed can never be
; stolen by a run of chomps, and CMD_NOTE_OFF on that track reaches the bed and
; nothing else. Samples live in slots and the slots are the generator's; see
; pacman-sound.ts.

; --- the samples reach the chip. Once, at startup, after the sprites.
; DESIGN: the loop flag rides on the slot and CMD_DEF_SAMPLE clears it, so each
; held sound asks for it again right after its define. The generator emits both
; halves together for exactly that reason; see the note in genpacsound.mjs.
sndinit:
        ; slot 0: chomp, the upper tone, 480 bytes
        OUT APU_CART_BANK, get_bankbyte(smpwka)
        OUT APU_CART_HI, get_highbyte(smpwka)
        OUT APU_CART_LO, get_lowbyte(smpwka)
        OUT APU_ARG, 1
        OUT APU_ARG2, 224
        OUT APU_NOTE, 60
        OUT APU_SLOT, 0
        OUT APU_CMD, CMD_DEF_SAMPLE
        ; slot 1: chomp, a whole tone down, 480 bytes
        OUT APU_CART_BANK, get_bankbyte(smpwkb)
        OUT APU_CART_HI, get_highbyte(smpwkb)
        OUT APU_CART_LO, get_lowbyte(smpwkb)
        OUT APU_ARG, 1
        OUT APU_ARG2, 224
        OUT APU_NOTE, 60
        OUT APU_SLOT, 1
        OUT APU_CMD, CMD_DEF_SAMPLE
        ; slot 2: the hunting siren, held, 3200 bytes
        OUT APU_CART_BANK, get_bankbyte(smpsrn)
        OUT APU_CART_HI, get_highbyte(smpsrn)
        OUT APU_CART_LO, get_lowbyte(smpsrn)
        OUT APU_ARG, 12
        OUT APU_ARG2, 128
        OUT APU_NOTE, 60
        OUT APU_SLOT, 2
        OUT APU_CMD, CMD_DEF_SAMPLE
        OUT APU_ARG, 1       ; mode 1: repeat, so the sound can be held
        OUT APU_CMD, CMD_LOOP_SLOT
        ; slot 3: the fright warble, held, 1600 bytes
        OUT APU_CART_BANK, get_bankbyte(smpfrt)
        OUT APU_CART_HI, get_highbyte(smpfrt)
        OUT APU_CART_LO, get_lowbyte(smpfrt)
        OUT APU_ARG, 6
        OUT APU_ARG2, 64
        OUT APU_NOTE, 60
        OUT APU_SLOT, 3
        OUT APU_CMD, CMD_DEF_SAMPLE
        OUT APU_ARG, 1       ; mode 1: repeat, so the sound can be held
        OUT APU_CMD, CMD_LOOP_SLOT
        ; slot 4: eyes going home, held, 1200 bytes
        OUT APU_CART_BANK, get_bankbyte(smpeye)
        OUT APU_CART_HI, get_highbyte(smpeye)
        OUT APU_CART_LO, get_lowbyte(smpeye)
        OUT APU_ARG, 4
        OUT APU_ARG2, 176
        OUT APU_NOTE, 60
        OUT APU_SLOT, 4
        OUT APU_CMD, CMD_DEF_SAMPLE
        OUT APU_ARG, 1       ; mode 1: repeat, so the sound can be held
        OUT APU_CMD, CMD_LOOP_SLOT
        ; slot 5: a frightened ghost caught, 2400 bytes
        OUT APU_CART_BANK, get_bankbyte(smpeat)
        OUT APU_CART_HI, get_highbyte(smpeat)
        OUT APU_CART_LO, get_lowbyte(smpeat)
        OUT APU_ARG, 9
        OUT APU_ARG2, 96
        OUT APU_NOTE, 60
        OUT APU_SLOT, 5
        OUT APU_CMD, CMD_DEF_SAMPLE
        ; slot 6: the death, 9600 bytes
        OUT APU_CART_BANK, get_bankbyte(smpdie)
        OUT APU_CART_HI, get_highbyte(smpdie)
        OUT APU_CART_LO, get_lowbyte(smpdie)
        OUT APU_ARG, 37
        OUT APU_ARG2, 128
        OUT APU_NOTE, 60
        OUT APU_SLOT, 6
        OUT APU_CMD, CMD_DEF_SAMPLE
        RET

; --- silence the bed, whatever it was playing.
; Two callers, and each is a moment the bed must not carry on through. The
; first line of the program, so a Reset does not leave the last siren droning
; over the redraw it starts. And nextlvl, so the fade between two mazes is
; silent, which is what the arcade does.
; DESIGN: bedcur is cleared as well as the voice stopped. It is the byte sndbed
; compares against, so a stop that left it holding the old answer would leave
; sndbed believing a bed nobody can hear is still playing, and the rest of the
; level would run in silence.
sndoff: OUT APU_TRACK, 1
        OUT APU_ARG, 0       ; 0: every note on the track, whatever it is
        OUT APU_CMD, CMD_NOTE_OFF
        LD A <- 0
        LD [bedcur] <- A
        RET

; --- the bed: one held sound, chosen once a frame, changed only when the
; answer changes.
; DESIGN: the comparison at sbset is the whole point of this routine. A looping
; voice retriggered every frame restarts sixty times a second, which is not a
; siren, it is a 60 Hz buzz with a siren somewhere inside it. So the wanted bed
; is worked out as one byte and only a byte that differs from the one already
; playing is allowed to touch the chip.
; DESIGN: that byte carries both which sound and, for the siren, which step of
; the climb: 0 silence, 1 fright, 2 eyes, 3 to 7 the siren. One byte, so a
; single compare answers "the same sound, at the same pitch".
; DESIGN: eyes beat fright, and fright beats the siren. A pair of eyes crossing
; the maze is the loudest news on the board and stays audible while the others
; are still blue. The scan below leaves on the first eyes it meets and only
; remembers that it saw blue, which is that priority written as control flow.
; DESIGN: gstate is the first test, and anything but playing is silence. That
; is what takes the bed out from under the death sting: snddie fires the sting
; from contact, gstate goes to 2 in the next line, and this routine drops the
; bed on the next arrival at the frame loop. It is also what holds the silence
; through the dying pause and the game over screen. State 0 is unreachable
; here: gstate is 1 before main is ever entered and never goes back.
sndbed: LD A <- [gstate]
        CMP A, 1
        JZ sbplay
        LD A <- 0
        JMP sbset
sbplay: LD A <- 0
        LD [sbb] <- A
        LD A <- 4
        LD [sbn] <- A
        LD D1 <- blinky
sbl:    LD A <- [D1+5]
        CMP A, 3
        JZ sbeyes            ; eyes: nothing outranks them, so stop looking
        CMP A, 2
        JNZ sbnx
sbblue: LD A <- 1
        LD [sbb] <- A
sbnx:   LD D1 <- D1+8
        LD A <- [sbn]
        SUB A <- 1
        LD [sbn] <- A
        JNZ sbl
sbscan: LD A <- [sbb]
        JZ sbsiren
        LD A <- 1
        JMP sbset
sbeyes: LD A <- 2
        JMP sbset
sbsiren: JSR sndstep
        ADD A <- 3
sbset:  LD [bedw] <- A
        SUB A <- [bedcur]
        JZ sbsame            ; the same bed as last frame: leave it running
        LD A <- [bedw]
        LD [bedcur] <- A
        OUT APU_TRACK, 1
        OUT APU_ARG, 0
        OUT APU_CMD, CMD_NOTE_OFF
        LD A <- [bedw]
        JZ sbsame            ; silence: stopped, and nothing to start
        SUB A <- 1
        JZ sbfr
        LD A <- [bedw]
        SUB A <- 2
        JZ sbey
; the siren, at the step's own note. One sample for all 5 steps: the climb is
; the same warble played higher and therefore faster, which is what the arcade
; does as the maze empties.
        OUT APU_SLOT, 2
        LD A <- [bedw]
        SUB A <- 3           ; the step, 0 to 4
        LD [sbk] <- A
        ADD A <- [sbk]
        ADD A <- 60
        OUTA APU_NOTE
        JMP sbgo
sbfr:   OUT APU_SLOT, 3
        OUT APU_NOTE, 60
        JMP sbgo
sbey:   OUT APU_SLOT, 4
        OUT APU_NOTE, 60
; APU_TRACK is still the bed's, latched by the CMD_NOTE_OFF above. A port is a
; latch and holds what was last written to it, so the track is set once for the
; pair of commands.
sbgo:   OUT APU_ARG, 12
        OUT APU_CMD, CMD_TRIGGER
sbsame: RET

; --- which step of the climb the siren is on: 0 with the maze nearly full,
; 4 with it nearly empty.
; DESIGN: read off dots every frame rather than counted as the dots fall. dots
; is the input and the step is a function of it, so there is no second copy of
; the board's state to drift, and a count that is poked, reset or reloaded is
; answered correctly on the very next frame.
; DESIGN: the thresholds are fifths of THIS level's total, worked out once per
; maze by sndlvl, because the four shipped mazes hold 244, 276, 222 and 262
; dots. A table of absolute counts would put the climb at a different fraction
; of each maze, and the arcade's siren climbs with the board rather than with a
; number.
; DESIGN: only the low byte is compared. Every threshold fits a byte, four
; fifths of 276 being 220, so a high byte with anything in it means more than
; 255 dots are left, which is more than any threshold and therefore step 0.
sndstep: LD D1 <- dots
        LD A <- [D1]
        JZ sstlo
        LD A <- 0
        RET
sstlo:  LD A <- [D1+1]
        CMP A, [srn4]
        JC sst4
        CMP A, [srn3]
        JC sst3
        CMP A, [srn2]
        JC sst2
        CMP A, [srn1]
        JC sst1
        LD A <- 0
        RET
sst1:   LD A <- 1
        RET
sst2:   LD A <- 2
        RET
sst3:   LD A <- 3
        RET
sst4:   LD A <- 4
        RET

; --- the level's siren thresholds, a fifth of its own dot count apart. Called
; from the end of the draw, where dots holds the total the pass just counted.
; DESIGN: repeated subtraction, because the machine has no divide. A fifth of
; 276 is 55 passes of the loop below, about 600 instructions once per level,
; against the 174,000 the draw already spends. The alternative, dividing on the
; fly, would pay that every frame instead of once a maze.
sndlvl: LD D1 <- dots
        LD A <- [D1]
        LD [srh] <- A
        LD A <- [D1+1]
        LD [srl] <- A
        LD A <- 0
        LD [srn4] <- A       ; the fifth, counted up as the total comes down
sl5:    LD A <- [srh]
        JZ sl5lo
        LD A <- [srl]        ; 256 or more still to go, so five certainly fits
        SUB A <- 5
        LD [srl] <- A
        JNC sl5inc
sl5hi:  LD A <- [srh]
        SUB A <- 1
        LD [srh] <- A
        JMP sl5inc
sl5lo:  LD A <- [srl]
        SUB A <- 5
        JC sl5done           ; under five left over: the count is the fifth
        LD [srl] <- A
sl5inc: LD A <- [srn4]
        INC A
        LD [srn4] <- A
        JMP sl5
sl5done: LD A <- [srn4]
        SHL A
        LD [srn3] <- A
        ADD A <- [srn4]
        LD [srn2] <- A
        ADD A <- [srn4]
        LD [srn1] <- A
        RET

; --- the chomp, on a dot and only on a dot. A pill is not a chomp: it is the
; moment the hunt turns round, and the bed changing to the fright warble is
; what says so.
; DESIGN: two tones, alternating. Either one on its own is a blip. The pair is
; the sound the game is remembered for, and the alternation costs one byte.
sndwaka: LD A <- [wakaph]
        JZ swa
        LD A <- 0
        LD [wakaph] <- A
        OUT APU_SLOT, 1
        JMP swgo
swa:    LD A <- 1
        LD [wakaph] <- A
        OUT APU_SLOT, 0
swgo:   OUT APU_TRACK, 0
        OUT APU_NOTE, 60
        OUT APU_ARG, 110
        OUT APU_CMD, CMD_TRIGGER
        RET

; --- a frightened ghost is caught, and the death. Two one shots that share
; everything but a slot and a level, so they share the tail as well.
; DESIGN: both on the sting track, so neither can be cut short by a chomp and
; neither can touch the bed. sndeat jumps the length of snddie to reach the
; tail; the RET at the end of it answers whichever of the two was called.
sndeat: OUT APU_SLOT, 5
        JMP sndsting
snddie: OUT APU_SLOT, 6
sndsting: OUT APU_TRACK, 2
        OUT APU_NOTE, 60
        OUT APU_ARG, 127
        OUT APU_CMD, CMD_TRIGGER
        RET

.ram
; 768 bytes for the palette the GPU reads on command, named without
; storing anything there.
palbuf: .addr($2000)
; DESIGN: every scalar is declared before work, so it lands in the zero
; page where [addr8] addressing reaches it. work is 868 bytes and would
; push anything declared after it out of reach.
drow:   db 0
dcol:   db 0
dch:    db 0
px:     db 0
py:     db 0
t1:     db 0
t2:     db 0
rx0:    db 0                   ; the corners rectxy draws between
ry0:    db 0
rx1:    db 0
ry1:    db 0
; --- wallpaint's eight neighbour flags, nonzero when that neighbour is not
; a wall. Each is read at most once per wall tile and then consulted many
; times over: the horizontal span alone reads nlf twice and nrt twice, and
; calling openat again for each would repeat a rowtab lookup for an answer
; already in hand. The four diagonals are read only where an inner corner
; is possible at all, so on most tiles some of them are never read. See
; wallpaint.
nup:    db 0                   ; the four orthogonals
ndn:    db 0
nlf:    db 0
nrt:    db 0
nul:    db 0                   ; the four diagonals
nur:    db 0
ndl:    db 0
ndr:    db 0
; --- where the maze sits on the screen: the pixel row of tile row 0.
; DESIGN: the maze is 248 pixels tall on a 256 pixel screen, and the score
; strip takes the eight rows above it. Every record, table and tunnel still
; works in maze pixels, where tile row 0 is pixel 0. So p2t and the tunnel
; wrap never see the strip. The offset is added at the four places a maze
; pixel reaches the GPU: rectxy, dotrect, pillrect and adxy. A byte in RAM
; rather than a literal, because the assembler has no named constant. Four
; copies of an 8 would drift apart.
mzytop: db 8
; --- and the pixel column of tile column 0, which centres the maze.
; DESIGN: computed, not shipped. A 28 column maze is 224 pixels wide on a 256
; pixel screen, so 16 pixels are spare and the maze takes half of them on
; each side. mzcols is the width the offset is worked out from, and mzxleft
; is (256 - mzcols * 8) / 2, which is 128 - mzcols * 4, written by mzcentre
; at the top of every drawmaze. A maze of another width writes mzcols before
; its draw and lands centred with no other change here. mzcols is also the
; count the draw loop, mkrows and the grid tests hold as a literal 28, and
; the 868 byte copy in loadmaze is 28 by 31: a maze of another width has to
; change those as well, so today every maze is 28 wide and mzcols never
; moves. Attract sets mzxleft to 0, because its chase has no maze under it
; and lays its actors out in screen pixels. The offset goes on at the same
; four places as mzytop, and nowhere else: every record, the tunnel table,
; contact and wrap work in maze pixels, where tile column 0 is pixel 0.
mzcols: db 28
mzxleft: db 0
; the maze index, 1 to 4: which of the four mazes is on screen
level:  db 1
dots:   dw 0
; --- the four bytes the score strip prints, in template order, so hud hands
; printf the game's own state and keeps no copy of it. score is big-endian,
; the way %u reads it.
score:  dw 0
; DESIGN: lives ships at 3, the arcade's own count, and is counted DOWN. Zero
; is the game over, so nothing anywhere compares against the 3.
lives:  db 3
; the difficulty level, 1 upward. level above is the maze and wraps at 4.
; This one never wraps, because a player on the fifth board is on level 5.
lvlno:  db 1
; --- the actor window: the one actor the movement code is working on.
; DESIGN: a window, not a pointer. canmove and advance each need D1 for a
; table lookup of their own, so a movement routine cannot also hold a pointer
; to the actor it is stepping and still do its own work. canmove loads D1 for
; dxtab and again for dytab, and reaches rowtab through tileat on top of that;
; advance loads it twice, once per table. loadact copies eight bytes in
; and saveact six back out, and every one of stage 1's movement routines then
; reads these names instead of Pac-Man's own. Measured price: 37 instructions
; per actor per step, the fourteen byte moves plus the calls that carry them,
; against 16,354 instructions in a real frame. The return is that one copy of
; the movement code drives five actors.
ax:     db 0                   ; pixel x
ay:     db 0                   ; pixel y
adir:   db 0                   ; 0 right, 1 down, 2 left, 3 up
aacc:   db 0                   ; speed accumulator
aspd:   db 0                   ; speed, a fraction of a pixel a frame times 256
astate: db 0                   ; 0 house, 1 normal, 2 frightened, 3 eyes
ascol:  db 0                   ; scatter corner column
asrow:  db 0                   ; scatter corner row
acur:   dw 0                   ; the record the window was loaded from
acti:   db 0                   ; which actor: 0 Pac-Man, 1 to 4 the ghosts
aastep: db 0                   ; the walk: 1 steps every actor, 0 only draws
; nextlvl borrows the walk to redraw all five sprites onto the new maze, which
; costs it all three of the scalars above. These hold what to hand back. See
; the DESIGN note in nextlvl for what each one does when it is not handed back.
nlacti: db 0
nlastep: db 0
nlacur: dw 0
; --- the buffered turn. Not a window field: it is the pad's memory, and only
; Pac-Man has a pad. A ghost decides from the maze, with nothing to buffer.
pacnext: db 3
t3:     db 0
t4:     db 0
aspr:   db 0
pacvis: db 0                   ; sprite 1 is shown, so its upper copy may be
plspr:  db 0                   ; the ghost sprite paclayer is testing
wdx:    db 0                   ; wdim's offset into the tile
wdy:    db 0
cx0:    db 0                   ; cellpart's corners, offsets into the tile
cy0:    db 0
cx1:    db 0
cy1:    db 0
adfr:   db 0                   ; a ghost's frame before the skirt is added
gs0:    db 0
gs1:    db 0
gs2:    db 0
; --- every tunnel the maze being played declares. A mouth is a TILE, not an
; edge: an actor whose centre lands on one comes out at the other mouth of its
; pair. That covers the three shapes the shipped mazes use with one rule --
; a pair on the outer columns, Metro Station's pair at columns 1 and 26 which
; never leaves the grid at all, and Photo Opportunity's pair joining row 4 to
; row 27 -- where the edge test this replaced covered only the first.
; A slot per digit, '1' to '9', so nine of them; slot d - 1 for digit d. Four
; bytes a slot, the two mouths as pixel positions: ax, ay, bx, by. tuncount is
; the highest digit the maze declared, which is how many slots are in use.
; DESIGN: a parallel table, not two more bytes a slot. tundtb holds the
; direction from each mouth toward its one open neighbour, 0 right, 1 down,
; 2 left, 3 up, which is the direction an actor leaves the far mouth by. It is
; separate so tuntab stays four bytes an entry, the layout the spec's memory
; budget names and the tunnel tests read.
; $FF in a tundtb slot means the slot is empty. A direction is 0 to 3, so $FF
; is not one, and the draw uses it to tell a pair's first mouth from its
; second. Both tables are per level: drawmaze clears them, see the note there.
tuncount: db 0
tuntab: db 0, 0, 0, 0
        db 0, 0, 0, 0
        db 0, 0, 0, 0
        db 0, 0, 0, 0
        db 0, 0, 0, 0
        db 0, 0, 0, 0
        db 0, 0, 0, 0
        db 0, 0, 0, 0
        db 0, 0, 0, 0
tundtb: db $FF, $FF
        db $FF, $FF
        db $FF, $FF
        db $FF, $FF
        db $FF, $FF
        db $FF, $FF
        db $FF, $FF
        db $FF, $FF
        db $FF, $FF
; --- the tunnel scratch. tund is the slot the draw is recording into and tunw
; the way out it just read off the maze; both belong to the draw alone. tunt is
; a one byte temporary, used by every tunnel routine and live across nothing.
; tunn counts the slots left to scan. The last four are tunfind's answer: tunmd
; is the way out of the mouth the actor is standing on, and tunpx, tunpy and
; tunpd are the other mouth of that pair and its own way out.
tund:   db 0
tunw:   db 0
tunt:   db 0
tunn:   db 0
tunmd:  db 0
tunpx:  db 0
tunpy:  db 0
tunpd:  db 0
; --- the ghost house, as the draw found it in the maze being drawn. Pixel
; positions, like every actor coordinate, not tile numbers.
doorx:  db 0                   ; the left of the two door cells
doory:  db 0
housex: db 0                   ; the H cell, which is inside the house
housey: db 0
outdy:  db 0                   ; door to outside: 8 (down) or 248 (up, -8)
; --- the release schedule. houstk is one countdown per actor, indexed by
; acti exactly as the actor walk is, and houphs is the divider they count on:
; actall steps it once a frame and a countdown moves one frame in eight. Both
; are written by placeghosts, so both are per level. Pac-Man's entry and
; Blinky's are never read, neither of them ever being in the house.
houstk: db 0, 0, 0, 0, 0
houphs: db 0
; --- which actor in the house got there by being EATEN, one byte each, the
; shape houstk has and indexed the same way. Nonzero means the ghost walked
; home as eyes, so it leaves the house normal even with a pill still standing.
; ehin sets it, ashfree spends it, and placeghosts clears all five, because a
; mark left standing over a level change or a death would send the next
; release out normal on a board of blue ghosts. Pac-Man's entry is never read,
; for houstk's own reason.
revive: db 0, 0, 0, 0, 0
; --- which strip each sprite is wearing, one byte an actor, indexed like the
; two above. 1 to 4 is that ghost's own colours, 5 Pac-Man, 6 the frightened
; strip, 7 the eyes. 0 is none, which is what they ship as and what the .ram
; image puts back on a Restart, so the first draw always defines.
; DESIGN: a cache and not a second source of truth. adstrip works the answer
; out from the state byte every frame and this only says whether it has been
; applied, so the two cannot drift the way state and art did.
astrip: db 0, 0, 0, 0, 0
adsk:   db 0                   ; the strip adstrip has just decided on
; --- the scatter and chase clock. mode is 0 for scatter and 1 for chase,
; modetk the ticks of the mode still owed, modetk8 the frame divider it counts
; on at eight frames a tick, and modefl the reversal a flip owes every ghost
; that is out of the house.
; DESIGN: all four ship as zero, including modetk, which is never a legal
; duration. drawmaze writes all four before the frame loop is ever reached, so
; these bytes are dead; declaring the real 52 here would let a drawmaze that
; stopped resetting them look right on level 1 and only go wrong from level 2,
; which is the shape the ghost records are declared zero to avoid.
mode:   db 0
modetk: db 0
modetk8: db 0
modefl: db 0
; --- the power pill. frtk is the units of the fright window still owed, on
; the mode clock's own divider, and zero means no pill is standing, so the
; countdown is its own flag. It is per level, and unfright is the one place
; that clears it; see the note there for the three moments that call it.
; The other three are scratch, live inside one routine and no longer: frg
; walks the four ghost records in frighten and unfright, and frcand and frn
; are the direction a frightened ghost is trying and how many tries are left.
frtk:   db 0
frg:    db 0
frcand: db 0
frn:    db 0
; What the NEXT ghost caught on this pill is worth, in tens. frighten loads it
; and cteat spends it, doubling as it goes, so it belongs to the pill rather
; than to the level or to the window. Zero until the first pill of a game is
; eaten, which costs nothing: no ghost is frightened before then, so nothing
; can be caught and the byte is never read.
ghchain: db 0
; --- the tile a ghost is steering at. target writes it, and dist2 measures
; against it. One pair, not one per ghost: the actor walk works on one ghost
; at a time and the choice is over before the next one is loaded.
tcol:   db 0
trow:   db 0
; --- target's own scratch. pmdir is Pac-Man's facing, read out of his record
; rather than out of adir, which belongs to the ghost in the window. tgn is
; how many steps of it are still owed, four for Pinky and two for Inky. bcol
; and brow are Blinky's tile, which only Inky's rule reads.
pmdir:  db 0
tgn:    db 0
bcol:   db 0
brow:   db 0
; --- dist2's answer, high byte first like every word here. d2shi and d2slo
; hold the second leg's square while it is added in, and must stay next to
; each other in that order: LD [d2hi] <- D2 and LD [d2shi] <- D2 each write
; two bytes.
d2hi:   db 0
d2lo:   db 0
d2shi:  db 0
d2slo:  db 0
d2leg:  db 0                   ; one axis's difference, clamped to 63
; --- pickdir's running best: the nearest squared distance seen so far, and
; the direction that scored it. pdcand is the candidate being tried, and
; gcol/grow the ghost's own tile, read once and stepped from once per
; candidate that is not the reversal, which is three of the four.
bsthi:  db 0
bstlo:  db 0
bstdir: db 0
pdcand: db 0
gcol:   db 0
grow:   db 0
; --- the distance field's own scratch. hdd is what a neighbour of the cell
; being expanded is worth, hcur is that cell's address,
; and hqh and hqt are the flood queue's head and tail. Each of those four is a
; word, high byte first like every word here, and its two halves must stay
; adjacent and in that order: LD D1 <- [hcurh] and LD [hcurh] <- D1 each carry
; two bytes. hct is one byte of scratch, the row count inside mkhrows. hbest, hbdir and hcand are eyedir's running best and the
; candidate it is trying, pickdir's bstlo/bstdir/pdcand in the shape a one
; byte distance needs.
; DESIGN: three bytes of their own, and NOT shared with that trio, though the
; aliasing argument would carry: a ghost takes one arm of ghdecide or the
; other and never both, which is the argument gcol and grow are already shared
; on. What differs is what a reader pays. gcol and grow hold the same quantity
; in both routines, the ghost's own tile, so sharing them costs nothing to
; read. bstlo is the low half of a squared distance where hbest is a count of
; steps, so writing one through the other would put the aliasing argument
; between a reader and every line that touches it, and buy three bytes of a
; zero page with 33 spare. If the zero page ever runs short, these three are
; the first place to look.
hdd:    db 0
hcurh:  db 0
hcurl:  db 0
hqhh:   db 0
hqhl:   db 0
hqth:   db 0
hqtl:   db 0
hct:    db 0
hbest:  db 0
hbdir:  db 0
hcand:  db 0
mainfr: db 0
fstep:  db 0
fframe: db 0
fwb:    db 0
fr:     db 0
fg:     db 0
fdb:    db 0
pacfrm: db 0                   ; the strip frame currently shown
pacph:  db 0                   ; mouth phase 0, 1, 2
pacpd:  db 0                   ; 0 opening, 1 closing
pactk:  db 0                   ; frames until the next phase step
; --- the game as a whole, rather than one actor.
; gstate is what the frame loop dispatches on: 0 ready, 1 playing, 2 dying,
; 3 game over. 0 stands for the whole of newgame's draw and fade, and the
; line that ends it is the one just before the loop is entered. The attract
; screen and the game over screen run outside the loop and leave it alone.
; lives sits beside score, in the block the score strip prints.
gstate: db 0
dietk:  db 0                   ; frames left in the dying pause
; --- the level table's row, as a pointer that walks. lvlrem is the advances
; still owed before the last row; see lvlstep.
lvlptr: dw 0
lvlrem: db 0
ghspd:  db 0                   ; this level's ghost speed, for ghcom
ghtun:  db 0                   ; and its speed inside a tunnel, for asacc
; --- intunnel's scratch, live inside one call and no longer. itn counts the
; pairs down, itmx, itmy and itmd are the mouth being measured, and itt is the
; coordinate itnear compares against. Its own, not tunfind's: canmove reaches
; tunfind on the same tick this runs on.
itn:    db 0
itmx:   db 0
itmy:   db 0
itmd:   db 0
itt:    db 0
; --- Pac-Man's eating pause, in frames. Set by eat and spent by asacc, so it
; outlives the tick that writes it on purpose: a pill is 3 frames and there
; is only one write.
pacstl: db 0
; --- Pac-Man's spawn, written by dmpac beside his record's own position, so
; a death has somewhere to put him back. See the note there.
pacsx:  db 0
pacsy:  db 0
; --- contact's scratch. cpx and cpy are Pac-Man's pixel position, cgx, cgy
; and cgs the ghost being measured, cgn the ghosts left to measure and cgp the
; record the walk stands on. The squared distance itself borrows dist2's
; d2 pair and d2leg; see the DESIGN note at contact for why that is safe.
cpx:    db 0
cpy:    db 0
cgx:    db 0
cgy:    db 0
cgs:    db 0
cgn:    db 0
cgp:    dw 0
; --- the sound. bedcur is the held sound the chip is playing and bedw the one
; this frame wants, both in sndbed's own encoding: 0 silence, 1 fright, 2 eyes,
; 3 and up the siren at that step of its climb. They are the pair the whole
; design turns on, because the bed changes only where they differ.
; DESIGN: bedcur is the game's copy of what the chip is doing, so the two can
; disagree and the game would then never notice. Restart is safe by itself: it
; powers the chip on silent and reloads this byte to the 0 declared here, so
; both say silence. Reset does neither, which is what sndoff on the program's
; first line is for.
bedcur: db 0
bedw:   db 0
; sndbed's own scratch, live inside one call and no longer. sbn counts the four
; ghost records down, sbb remembers whether a blue one was seen, and sbk holds
; the siren step while its note is worked out.
sbn:    db 0
sbb:    db 0
sbk:    db 0
; sndlvl's 16 bit remainder, which it counts down by fifths. Live once a level.
srh:    db 0
srl:    db 0
; --- the siren's thresholds, 4 of them, highest first: the dot counts the
; climb steps at. srn1 is four fifths of this level's total and srn4 is one
; fifth. Written by sndlvl at the end of every draw, so they belong to the maze
; being played rather than to the program.
srn1:   db 0
srn2:   db 0
srn3:   db 0
srn4:   db 0
wakaph: db 0                   ; which of the two chomp tones is next
; --- the screens around the game. padprev is the pad level anykey compares
; against, and blinkst the last state of the blinking line. atph and attk are
; the attract screen's phase and its frames in that phase. atrow is the text
; row the next roster entry goes on and atnptr the next name in atnames. atpx
; and atx are the x of Pac-Man and of the ghost being drawn in the chase.
padprev: db 0
blinkst: db 0
atph:   db 0
attk:   db 0
atrow:  db 0
atnptr: dw 0
atpx:   db 0
atx:    db 0
; --- bigtext's scratch. bgx and bgy are the corner the caller sets, bgptr
; the string and bgg the glyph's offset. bgr and bgc are the row and column
; of the bitmap being walked. bgbits holds the row's bits, and bgpx, bgpy the
; block's pixel.
bgx:    db 0
bgy:    db 0
bgptr:  dw 0
bgg:    db 0
bgr:    db 0
bgc:    db 0
bgbits: db 0
bgpx:   db 0
bgpy:   db 0
; --- the actors. Five records of eight bytes, laid out exactly like the
; window: x, y, dir, acc, spd, state, scol, srow. A record is the durable home
; of an actor's position; the window is where it is worked on for one step.
; DESIGN: Pac-Man's first five fields keep the names stage 1 gave them as
; standalone scalars, because that is still what they are. dmpac and
; drawmaze's per-level reset write his starting position, direction and
; accumulator, and they write them here, to the record, not to the window: the
; window belongs to whichever actor is mid-step, and at startup no actor has
; been loaded into it at all. Everything that runs during a step reads the
; window instead, and actall is what joins the two: load, step, save, draw.
actors:
pacman:
pacx:   db 0                   ; 0: pixel x, where dmpac found his P marker
pacy:   db 0                   ; 1: pixel y
; DESIGN: default up, not right. At CLASSIC's spawn tile the corridor runs
; only left and right, both open; up and down are walls there. A default
; of 0 (right) would start him walking before any key is pressed, so the
; default has to be a direction the spawn tile actually blocks. Checked
; against all four mazes, not only CLASSIC: the rightmost P sits at column
; 14 of rows 23, 22, 11 and 19, and the tile above and below is wall in
; every one, so 3 holds as the reset value drawmaze writes back on every
; level clear, not only as this initial RAM value.
pacdir: db 3                   ; 2: direction
pacacc: db 0                   ; 3: speed accumulator
pacspd: db 205                 ; 4: 80 percent of a pixel a frame
        db 1                   ; 5: state, normal
        db 0, 0                ; 6, 7: scatter corner, which he never uses
; The ghosts. Nothing steps them yet: task 4 walks this array. These initial
; bytes are never read: placeghosts overwrites all eight fields of all four
; records at the end of every drawmaze, which runs before the main loop
; starts and again on every level clear. They stay zero rather than carrying
; CLASSIC's coordinates, so a placeghosts that stopped running would leave the
; ghosts stacked at the top left corner instead of quietly looking right on
; the first maze only.
blinky: db 0, 0, 0, 0, 0, 0, 0, 0
pinky:  db 0, 0, 0, 0, 0, 0, 0, 0
inky:   db 0, 0, 0, 0, 0, 0, 0, 0
clyde:  db 0, 0, 0, 0, 0, 0, 0, 0
work:   db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0

rowtab: dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0

; --- the distance field: how many steps from the house each tile is, one byte
; a cell, rewritten by floodhome on every draw. 30 cells wide and 34 tall where
; the maze is 28 by 31, the difference being a border of wall and one spare
; row; hdcell owns that shape and says what each part of it is for. Past work
; with rowtab, for the reason the note below gives: nothing reads it with
; [addr8], only through a D register.
homedist: db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0

; --- the field's row table, one address per field row. Built once by mkhrows.
hrowtab: dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0

; --- the flood's queue: the cells whose distance is settled and whose
; neighbours have not been looked at yet, two bytes of address each. A cell is
; written once and pushed once, so 868 entries is one per tile of the maze and
; no maze can overflow it.
hq:     dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0
        dw 0

; DESIGN: dxtab and dytab are read with LD D1 <- table then [D1+A], never
; with bare [addr8], so they do not need the zero page and belong here,
; past work, alongside rowtab. p2t and sqtab below are read the same way and
; are here for the same reason, and in their case it is not a preference:
; together they are 384 bytes, so declaring them with the scalars would push
; every scalar after them out of the 256 byte zero page that [addr8]
; addressing reaches.
dxtab:  db 1, 0, $FF, 0      ; right, down, left, up
dytab:  db 0, 1, 0, $FF
; --- a whole tile's step, the same four directions. wrap lands an actor one
; tile on from the mouth it comes out of, on that mouth's own corridor tile,
; so it steps eight pixels where advance steps one. $F8 is minus eight.
dx8tab: db 8, 0, $F8, 0
dy8tab: db 0, 8, 0, $F8
; --- a pixel coordinate to its tile, one entry per byte value. Read as
; p2t[ax] and p2t[ay] by pactile. Generated in pacman-src.ts, not typed.
p2t:    db 0,0,0,0,0,0,0,0
        db 1,1,1,1,1,1,1,1
        db 2,2,2,2,2,2,2,2
        db 3,3,3,3,3,3,3,3
        db 4,4,4,4,4,4,4,4
        db 5,5,5,5,5,5,5,5
        db 6,6,6,6,6,6,6,6
        db 7,7,7,7,7,7,7,7
        db 8,8,8,8,8,8,8,8
        db 9,9,9,9,9,9,9,9
        db 10,10,10,10,10,10,10,10
        db 11,11,11,11,11,11,11,11
        db 12,12,12,12,12,12,12,12
        db 13,13,13,13,13,13,13,13
        db 14,14,14,14,14,14,14,14
        db 15,15,15,15,15,15,15,15
        db 16,16,16,16,16,16,16,16
        db 17,17,17,17,17,17,17,17
        db 18,18,18,18,18,18,18,18
        db 19,19,19,19,19,19,19,19
        db 20,20,20,20,20,20,20,20
        db 21,21,21,21,21,21,21,21
        db 22,22,22,22,22,22,22,22
        db 23,23,23,23,23,23,23,23
        db 24,24,24,24,24,24,24,24
        db 25,25,25,25,25,25,25,25
        db 26,26,26,26,26,26,26,26
        db 27,27,27,27,27,27,27,27
        db 28,28,28,28,28,28,28,28
        db 29,29,29,29,29,29,29,29
        db 30,30,30,30,30,30,30,30
        db 31,31,31,31,31,31,31,31
; --- the squares of 0 to 63, big-endian words, indexed by twice the leg.
; dist2 reads it once per axis, and contact reads it the same way in pixels.
; Generated in pacman-src.ts, not typed.
sqtab:  dw 0,1,4,9,16,25,36,49
        dw 64,81,100,121,144,169,196,225
        dw 256,289,324,361,400,441,484,529
        dw 576,625,676,729,784,841,900,961
        dw 1024,1089,1156,1225,1296,1369,1444,1521
        dw 1600,1681,1764,1849,1936,2025,2116,2209
        dw 2304,2401,2500,2601,2704,2809,2916,3025
        dw 3136,3249,3364,3481,3600,3721,3844,3969
; --- the per-level difficulty table, 21 rows of 7 bytes. The CPU cannot
; read the cartridge, so the startup block copies lvlrom here with one
; CMD_COPY and lvlptr walks this copy. Past work with rowtab, p2t and sqtab,
; for the reason the note above them gives: nothing reads it with [addr8].
lvltab: db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0
        db 0

; --- three bytes an entry: bank, high, low. Big-endian, like every address.
; loadmaze walks this to find the current level's maze.
; DESIGN: OVERRIDE. .ram, not .data. .data compiles into the cartridge
; image, a separate byte array the GPU and audio chip read and the CPU
; cannot; loadmaze must dereference this table with LD D1 <- mazetab and
; [D1+n] to pick a level at runtime, and a CPU read of a .data address
; silently returns whatever real data RAM happens to sit at that same
; numeric offset. get_bankbyte, get_highbyte and get_lowbyte still resolve
; at assembly time, so they work here exactly as they do in .data; only the
; section changes, to the one the CPU can actually read.
; --- the four ghost strips, three bytes an entry, in the order the records
; are laid out: Blinky, Pinky, Inky, Clyde. ghstrip walks it, at startup to
; create the four sprites and on every frame a ghost's state asks for its own
; colours. .ram for mazetab's own reason, spelled out there: the
; CPU cannot read .data, and a table picked between at runtime has to be
; dereferenced by the CPU.
ghsttab: db get_bankbyte(ghost0spr), get_highbyte(ghost0spr), get_lowbyte(ghost0spr)
        db get_bankbyte(ghost1spr), get_highbyte(ghost1spr), get_lowbyte(ghost1spr)
        db get_bankbyte(ghost2spr), get_highbyte(ghost2spr), get_lowbyte(ghost2spr)
        db get_bankbyte(ghost3spr), get_highbyte(ghost3spr), get_lowbyte(ghost3spr)
mazetab: db get_bankbyte(maze1), get_highbyte(maze1), get_lowbyte(maze1)
        db get_bankbyte(maze2), get_highbyte(maze2), get_lowbyte(maze2)
        db get_bankbyte(maze3), get_highbyte(maze3), get_lowbyte(maze3)
        db get_bankbyte(maze4), get_highbyte(maze4), get_lowbyte(maze4)
; --- the screens' text, in RAM because puts reads it with the CPU. The
; roster names are packed and walked by atnptr, one name per phase. They are
; in the order of the records and the sprites: Blinky, Pinky, Inky, Clyde,
; then Pac-Man. The blank line is the blinking line's off state, the same width.
atnames: db "BLINKY - SHADOW", 0
        db "PINKY - SPEEDY", 0
        db "INKY - BASHFUL", 0
        db "CLYDE - POKEY", 0
        db "PAC-MAN", 0
atpress: db "PRESS A KEY TO START", 0
atblank: db "                    ", 0
gopress: db "PRESS A KEY", 0
; --- the block letter font bigtext draws: the eleven letters the two big
; lines need, 5 wide by 7 high. A row is one byte, with bit 4 the left
; column. The letters are P A C - M N G E O V R, seven bytes apart from
; offset 0.
bigfont: db $1E, $11, $11, $1E, $10, $10, $10   ; P
        db $0E, $11, $11, $1F, $11, $11, $11    ; A
        db $0E, $11, $10, $10, $10, $11, $0E    ; C
        db $00, $00, $00, $1F, $00, $00, $00    ; -
        db $11, $1B, $15, $15, $11, $11, $11    ; M
        db $11, $19, $15, $13, $11, $11, $11    ; N
        db $0E, $11, $10, $17, $11, $11, $0F    ; G
        db $1F, $10, $10, $1E, $10, $10, $1F    ; E
        db $0E, $11, $11, $11, $11, $11, $0E    ; O
        db $11, $11, $11, $11, $11, $0A, $04    ; V
        db $1E, $11, $11, $1E, $14, $12, $11    ; R
; the big lines, as offsets into bigfont, each ended by 255.
bigttl: db 0, 7, 14, 21, 28, 7, 35, 255       ; PAC-MAN
biggame: db 42, 7, 28, 49, 255                ; GAME
bigover: db 56, 63, 49, 70, 255               ; OVER
.data
; --- the sprites, one PNG strip each, beside this file. The IDE's sprite
; editor opens them from the Files pane. .sprite reads the frame count the
; PNG states and lays the strip out as CMD_SPRITE_DEF wants it.
; DESIGN: the strips are drawn on the machine palette. $FC is Pac-Man
; yellow under default332() (R=(i>>5)&7, G=(i>>2)&7, B=i&3): R7 G7 B0,
; RGB(255,255,0). The pupils are index 4, which the editor shows in the
; default palette's dark green: the program sets 4 to near black at start.
; No sprite colour may sit in 1 to 3, which the maze fades rewrite.
; DESIGN: every frame is 14 by 14, drawn by gensprites.py beside this
; file. Pac-Man is twelve frames, four facings times three mouth
; positions. A ghost is eight, two per facing, the second with the other
; skirt, so the body ripples as it moves. The frightened strip is four:
; blue, blue, white, white, each pair a skirt and the other. The eyes are
; eight, each facing twice, so a pair of eyes takes the same frame number
; as the ghost it was.
pacspr: .sprite('pacman.png')
ghost0spr: .sprite('blinky.png')
ghost1spr: .sprite('pinky.png')
ghost2spr: .sprite('inky.png')
ghost3spr: .sprite('clyde.png')
frightspr: .sprite('fright.png')
eyesspr: .sprite('eyes.png')
; DESIGN: scripts/genpacsound.mjs synthesises the seven samples the same way,
; from a handful of frequencies rather than from typed bytes. See
; pacman-sound.ts, and the sndinit block above for the defines that go with
; them. 8 bit unsigned at 8 kHz, centred on 128, which is what the chip plays.
smpwka: db 128, 132, 133, 114, 115, 106, 147, 153, 164, 126, 83, 88, 91, 184, 174, 197
        db 101, 57, 56, 83, 214, 191, 216, 94, 48, 53, 76, 211, 192, 217, 115, 42
        db 60, 54, 193, 199, 210, 160, 41, 65, 39, 143, 215, 194, 208, 77, 50, 53
        db 67, 201, 198, 210, 166, 44, 63, 41, 109, 216, 191, 217, 133, 39, 65, 39
        db 130, 217, 191, 217, 125, 39, 65, 39, 126, 217, 191, 217, 142, 40, 64, 41
        db 97, 211, 195, 210, 179, 56, 54, 54, 57, 180, 210, 195, 214, 109, 39, 65
        db 40, 105, 212, 196, 207, 191, 69, 47, 61, 42, 139, 217, 192, 213, 174, 57
        db 51, 58, 44, 147, 217, 191, 213, 176, 60, 49, 61, 41, 130, 216, 194, 208
        db 196, 82, 42, 65, 40, 92, 202, 205, 195, 215, 132, 42, 59, 53, 49, 151
        db 217, 193, 209, 196, 86, 40, 65, 43, 71, 182, 214, 191, 215, 178, 68, 44
        db 65, 40, 81, 190, 212, 191, 215, 177, 69, 43, 65, 42, 73, 180, 215, 191
        db 212, 193, 88, 39, 63, 49, 52, 149, 216, 196, 201, 213, 132, 45, 53, 62
        db 39, 95, 195, 212, 191, 213, 190, 88, 39, 62, 52, 45, 129, 211, 204, 194
        db 217, 171, 70, 41, 65, 48, 51, 141, 214, 201, 195, 217, 169, 69, 41, 64
        db 49, 48, 132, 211, 205, 193, 216, 183, 85, 39, 61, 56, 41, 104, 197, 213
        db 191, 209, 206, 122, 45, 50, 64, 41, 65, 159, 216, 199, 195, 217, 175, 80
        db 39, 61, 57, 40, 94, 188, 216, 193, 203, 215, 151, 61, 42, 64, 51, 43
        db 111, 198, 213, 192, 206, 212, 143, 57, 43, 64, 51, 43, 110, 197, 214, 192
        db 204, 214, 153, 64, 41, 63, 55, 40, 93, 184, 217, 195, 198, 217, 176, 85
        db 39, 57, 62, 40, 66, 153, 214, 205, 191, 212, 204, 125, 50, 45, 65, 50
        db 43, 104, 190, 216, 195, 198, 217, 179, 90, 40, 54, 64, 43, 55, 133, 206
        db 211, 191, 205, 215, 159, 73, 39, 59, 61, 40, 64, 147, 211, 208, 191, 207
        db 212, 152, 68, 39, 60, 60, 40, 65, 148, 210, 207, 189, 204, 210, 156, 76
        db 45, 63, 68, 49, 65, 135, 197, 202, 183, 191, 203, 166, 95, 56, 66, 77
        db 62, 63, 115, 177, 196, 180, 178, 193, 178, 121, 72, 68, 83, 77, 67, 94
        db 151, 185, 179, 168, 178, 181, 147, 96, 75, 85, 91, 80, 83, 121, 164, 176
        db 164, 163, 173, 164, 126, 91, 86, 97, 95, 87, 100, 136, 163, 163, 155, 158
        db 164, 149, 117, 96, 98, 105, 102, 98, 113, 140, 155, 152, 147, 151, 153, 139
        db 117, 105, 109, 113, 110, 109, 120, 137, 146, 143, 140, 142, 142, 134, 121, 116
        db 118, 120, 119, 119, 124, 132, 135, 134, 132, 133, 132, 130, 127, 126, 127, 127
smpwkb: db 128, 131, 137, 121, 112, 109, 109, 155, 157, 167, 146, 79, 88, 68, 122, 197
        db 180, 207, 118, 41, 64, 41, 150, 215, 192, 214, 103, 41, 64, 41, 150, 215
        db 192, 216, 116, 39, 65, 39, 123, 217, 192, 215, 156, 44, 61, 47, 74, 197
        db 204, 200, 205, 87, 42, 64, 39, 126, 216, 193, 212, 177, 58, 51, 58, 45
        db 151, 217, 191, 215, 167, 53, 53, 58, 45, 148, 217, 192, 213, 180, 64, 47
        db 63, 40, 117, 213, 198, 202, 207, 102, 39, 64, 45, 66, 179, 214, 191, 215
        db 172, 61, 46, 64, 39, 98, 202, 206, 194, 217, 150, 50, 51, 61, 39, 106
        db 205, 205, 194, 217, 156, 54, 48, 64, 39, 89, 193, 212, 191, 215, 183, 78
        db 40, 64, 47, 56, 154, 217, 196, 201, 213, 135, 47, 51, 63, 39, 84, 186
        db 215, 191, 209, 203, 111, 41, 56, 60, 39, 95, 193, 213, 191, 209, 203, 113
        db 42, 54, 62, 39, 82, 180, 216, 193, 203, 213, 142, 53, 45, 65, 45, 54
        db 142, 213, 204, 193, 216, 188, 93, 40, 57, 60, 39, 80, 175, 217, 196, 198
        db 217, 168, 74, 39, 61, 57, 40, 91, 183, 217, 195, 199, 217, 168, 76, 39
        db 60, 59, 39, 80, 171, 217, 199, 195, 216, 188, 99, 41, 52, 64, 43, 55
        db 136, 208, 210, 191, 207, 212, 146, 62, 41, 62, 57, 39, 81, 169, 216, 201
        db 192, 214, 201, 123, 50, 44, 65, 52, 41, 92, 179, 217, 199, 193, 215, 199
        db 121, 50, 44, 64, 54, 40, 85, 170, 216, 203, 192, 211, 208, 141, 61, 40
        db 61, 60, 40, 63, 142, 208, 211, 192, 202, 217, 176, 93, 42, 50, 65, 49
        db 42, 95, 177, 217, 202, 192, 211, 210, 149, 70, 39, 57, 63, 43, 49, 114
        db 191, 217, 198, 193, 213, 205, 139, 63, 39, 58, 63, 43, 50, 115, 191, 217
        db 199, 193, 212, 208, 147, 69, 39, 55, 64, 46, 45, 100, 179, 216, 204, 191
        db 207, 215, 169, 90, 42, 48, 65, 53, 39, 73, 149, 208, 213, 193, 197, 216
        db 198, 128, 59, 40, 58, 63, 44, 46, 103, 179, 215, 203, 189, 202, 212, 176
        db 104, 53, 51, 69, 66, 50, 64, 122, 184, 204, 189, 181, 195, 199, 161, 99
        db 60, 63, 78, 73, 60, 76, 127, 178, 193, 180, 174, 185, 188, 157, 105, 71
        db 73, 85, 83, 72, 82, 122, 166, 182, 173, 165, 173, 179, 158, 116, 85, 81
        db 92, 93, 84, 86, 114, 150, 170, 166, 158, 161, 168, 159, 130, 101, 91, 98
        db 103, 98, 94, 107, 134, 155, 159, 152, 150, 155, 155, 140, 118, 104, 104, 110
        db 110, 106, 108, 122, 139, 148, 145, 141, 142, 145, 141, 130, 118, 114, 116, 119
        db 118, 117, 120, 128, 135, 136, 134, 133, 133, 133, 131, 128, 126, 126, 127, 127
smpsrn: db 203, 197, 185, 168, 147, 125, 103, 83, 68, 57, 52, 54, 62, 75, 94, 115
        db 137, 158, 177, 192, 201, 204, 201, 191, 176, 157, 135, 113, 92, 74, 61, 53
        db 52, 57, 69, 85, 105, 127, 149, 169, 186, 198, 204, 203, 196, 183, 166, 145
        db 123, 101, 82, 66, 56, 52, 54, 63, 77, 96, 117, 140, 161, 179, 193, 202
        db 204, 200, 189, 174, 154, 132, 110, 89, 72, 59, 53, 53, 59, 71, 88, 109
        db 131, 153, 173, 189, 199, 204, 202, 194, 180, 162, 140, 118, 97, 78, 63, 54
        db 52, 56, 66, 82, 101, 123, 145, 166, 184, 196, 203, 204, 197, 185, 168, 147
        db 125, 103, 83, 67, 56, 52, 54, 62, 77, 95, 117, 139, 161, 179, 193, 202
        db 204, 200, 189, 173, 153, 131, 108, 88, 71, 58, 52, 53, 60, 73, 91, 112
        db 134, 156, 176, 191, 201, 204, 201, 191, 176, 157, 135, 112, 91, 73, 60, 53
        db 52, 59, 71, 88, 109, 131, 154, 174, 190, 200, 204, 202, 192, 178, 159, 137
        db 114, 93, 74, 61, 53, 52, 58, 70, 87, 108, 131, 153, 173, 189, 200, 204
        db 202, 192, 178, 158, 136, 114, 92, 74, 60, 53, 52, 59, 71, 88, 110, 132
        db 155, 175, 190, 201, 204, 201, 191, 176, 156, 133, 111, 89, 72, 59, 52, 53
        db 60, 74, 92, 114, 137, 159, 178, 193, 202, 204, 199, 188, 171, 151, 128, 105
        db 84, 68, 57, 52, 54, 63, 78, 98, 121, 144, 165, 183, 197, 203, 203, 196
        db 183, 164, 142, 119, 97, 77, 63, 54, 52, 57, 69, 86, 108, 131, 154, 174
        db 190, 201, 204, 201, 190, 174, 154, 131, 108, 86, 69, 57, 52, 54, 63, 78
        db 98, 121, 144, 166, 184, 197, 204, 203, 195, 181, 161, 139, 116, 93, 74, 60
        db 53, 53, 60, 73, 92, 114, 138, 160, 180, 195, 203, 204, 197, 184, 166, 144
        db 120, 97, 77, 62, 54, 52, 58, 71, 89, 111, 135, 158, 178, 193, 202, 204
        db 198, 186, 167, 145, 122, 98, 78, 63, 54, 52, 58, 71, 89, 111, 135, 158
        db 179, 194, 203, 204, 198, 184, 166, 143, 119, 96, 76, 61, 53, 52, 59, 73
        db 93, 116, 140, 163, 182, 196, 203, 203, 195, 180, 160, 137, 113, 90, 71, 58
        db 52, 54, 63, 79, 100, 124, 148, 170, 188, 200, 204, 201, 190, 172, 151, 126
        db 102, 81, 64, 54, 52, 57, 70, 89, 112, 136, 160, 180, 195, 203, 203, 195
        db 181, 160, 136, 112, 89, 70, 57, 52, 55, 65, 82, 104, 128, 153, 175, 192
        db 202, 204, 198, 185, 166, 142, 118, 94, 74, 59, 52, 53, 63, 79, 100, 125
        db 149, 172, 190, 201, 204, 199, 187, 168, 144, 119, 95, 75, 60, 52, 53, 62
        db 79, 100, 125, 150, 173, 190, 201, 204, 199, 185, 166, 142, 117, 92, 72, 58
        db 52, 54, 65, 82, 105, 130, 155, 177, 193, 203, 204, 196, 181, 160, 135, 110
        db 86, 67, 56, 52, 57, 70, 89, 113, 139, 163, 184, 198, 204, 201, 191, 173
        db 149, 124, 99, 77, 61, 53, 53, 62, 79, 101, 127, 152, 175, 192, 202, 204
        db 196, 181, 159, 134, 108, 84, 66, 55, 52, 58, 73, 94, 119, 145, 169, 188
        db 200, 204, 199, 185, 165, 140, 113, 89, 69, 56, 52, 57, 70, 90, 115, 141
        db 166, 186, 200, 204, 200, 186, 166, 141, 115, 90, 70, 56, 52, 57, 70, 91
        db 116, 142, 167, 187, 200, 204, 199, 185, 164, 138, 112, 87, 67, 55, 52, 58
        db 73, 95, 121, 148, 172, 191, 202, 204, 196, 180, 157, 131, 104, 80, 63, 53
        db 53, 62, 80, 104, 130, 157, 180, 196, 204, 202, 190, 171, 146, 119, 93, 71
        db 57, 52, 56, 70, 91, 117, 145, 170, 190, 201, 204, 196, 180, 157, 130, 103
        db 79, 61, 53, 54, 64, 83, 108, 136, 162, 184, 199, 204, 199, 185, 163, 137
        db 109, 84, 65, 54, 53, 62, 79, 104, 131, 158, 182, 197, 204, 200, 187, 166
        db 139, 111, 86, 66, 54, 52, 61, 79, 103, 131, 158, 182, 198, 204, 200, 186
        db 165, 138, 110, 84, 64, 53, 53, 63, 81, 107, 135, 162, 185, 199, 204, 198
        db 183, 160, 132, 104, 79, 61, 52, 54, 67, 88, 114, 143, 169, 190, 202, 203
        db 194, 176, 150, 122, 94, 71, 56, 52, 58, 74, 98, 127, 155, 180, 197, 204
        db 200, 186, 164, 136, 107, 81, 62, 53, 54, 66, 87, 114, 143, 170, 191, 202
        db 203, 193, 173, 147, 117, 90, 68, 55, 52, 61, 80, 106, 135, 163, 186, 200
        db 204, 196, 178, 153, 124, 95, 72, 56, 52, 59, 76, 101, 130, 159, 183, 199
        db 204, 198, 181, 156, 126, 97, 73, 57, 52, 58, 75, 100, 130, 159, 183, 199
        db 204, 198, 180, 155, 125, 96, 72, 56, 52, 59, 77, 103, 133, 162, 186, 200
        db 204, 196, 177, 150, 120, 91, 68, 55, 52, 62, 82, 110, 140, 168, 190, 203
        db 203, 191, 170, 141, 111, 83, 63, 53, 54, 68, 91, 120, 151, 177, 196, 204
        db 200, 184, 159, 129, 99, 73, 57, 52, 59, 78, 105, 135, 164, 188, 202, 203
        db 193, 171, 143, 112, 84, 63, 53, 55, 69, 93, 122, 153, 180, 198, 204, 198
        db 180, 154, 123, 93, 69, 55, 53, 63, 85, 113, 145, 173, 194, 204, 201, 186
        db 161, 130, 100, 73, 57, 52, 60, 80, 108, 139, 168, 191, 203, 202, 189, 165
        db 134, 103, 76, 58, 52, 59, 78, 105, 137, 167, 190, 203, 202, 189, 165, 135
        db 104, 76, 58, 52, 59, 78, 106, 138, 168, 191, 203, 202, 188, 163, 133, 101
        db 74, 57, 52, 61, 81, 110, 142, 172, 194, 204, 200, 184, 158, 127, 95, 70
        db 55, 53, 64, 87, 117, 150, 178, 197, 204, 197, 178, 150, 117, 87, 64, 53
        db 55, 70, 96, 128, 160, 186, 201, 203, 192, 168, 138, 105, 77, 58, 52, 60
        db 80, 109, 142, 172, 194, 204, 200, 182, 155, 122, 91, 66, 53, 54, 68, 94
        db 126, 158, 185, 201, 203, 192, 168, 137, 104, 76, 57, 52, 61, 83, 113, 146
        db 176, 196, 204, 198, 178, 149, 116, 85, 62, 52, 56, 74, 102, 136, 167, 191
        db 203, 201, 185, 158, 125, 92, 67, 53, 54, 69, 95, 128, 160, 187, 202, 203
        db 189, 163, 131, 98, 71, 55, 53, 66, 90, 123, 156, 184, 201, 203, 191, 167
        db 135, 101, 73, 56, 52, 64, 88, 120, 154, 182, 200, 204, 192, 168, 136, 102
        db 74, 56, 52, 64, 88, 120, 154, 183, 200, 204, 192, 167, 135, 101, 73, 55
        db 53, 65, 90, 123, 157, 185, 201, 203, 190, 164, 131, 97, 70, 54, 53, 68
        db 94, 128, 161, 188, 203, 202, 186, 159, 125, 92, 66, 53, 55, 72, 101, 135
        db 168, 193, 204, 199, 180, 151, 116, 84, 61, 52, 58, 79, 110, 145, 176, 197
        db 204, 195, 172, 140, 105, 76, 56, 52, 64, 89, 122, 156, 185, 202, 203, 188
        db 161, 127, 93, 67, 53, 55, 72, 102, 136, 169, 194, 204, 198, 178, 147, 112
        db 80, 59, 52, 61, 85, 117, 152, 182, 200, 203, 190, 164, 130, 95, 68, 53
        db 55, 72, 101, 136, 169, 194, 204, 198, 177, 146, 110, 79, 58, 52, 62, 87
        db 121, 156, 185, 202, 203, 187, 159, 125, 90, 64, 52, 57, 76, 107, 143, 175
        db 197, 204, 195, 170, 137, 102, 72, 55, 53, 68, 96, 131, 166, 192, 204, 199
        db 179, 148, 112, 80, 58, 52, 63, 88, 121, 157, 186, 202, 202, 186, 156, 121
        db 87, 62, 52, 59, 81, 113, 149, 180, 200, 203, 190, 163, 128, 93, 66, 53
        db 56, 75, 107, 143, 175, 198, 204, 194, 168, 134, 99, 69, 54, 54, 72, 102
        db 138, 171, 195, 204, 196, 172, 139, 103, 72, 55, 53, 69, 98, 134, 168, 194
        db 204, 197, 175, 142, 105, 74, 55, 53, 68, 96, 132, 167, 193, 204, 198, 176
        db 143, 107, 75, 56, 53, 67, 95, 131, 166, 192, 204, 198, 176, 144, 107, 75
        db 56, 53, 67, 95, 131, 166, 193, 204, 198, 176, 143, 106, 75, 55, 53, 68
        db 97, 133, 168, 193, 204, 197, 174, 141, 104, 73, 55, 53, 70, 99, 135, 170
        db 195, 204, 196, 172, 137, 101, 71, 54, 54, 72, 103, 139, 173, 197, 204, 194
        db 168, 133, 97, 68, 53, 56, 75, 107, 144, 177, 199, 204, 191, 163, 128, 92
        db 65, 52, 58, 80, 113, 150, 182, 201, 203, 187, 158, 121, 86, 61, 52, 60
        db 85, 120, 156, 186, 203, 201, 183, 151, 114, 80, 58, 52, 64, 91, 127, 163
        db 191, 204, 199, 177, 143, 106, 74, 55, 53, 69, 99, 136, 171, 195, 204, 195
        db 170, 135, 98, 68, 53, 55, 75, 107, 145, 178, 199, 204, 190, 162, 125, 89
        db 63, 52, 59, 83, 117, 154, 185, 202, 202, 184, 152, 115, 81, 58, 52, 64
        db 91, 127, 164, 191, 204, 198, 176, 142, 105, 73, 55, 54, 71, 101, 138, 173
        db 197, 204, 193, 167, 131, 94, 66, 52, 57, 79, 112, 150, 182, 201, 203, 186
        db 156, 119, 84, 60, 52, 62, 88, 124, 161, 190, 203, 199, 178, 145, 107, 75
        db 55, 53, 69, 99, 136, 171, 196, 204, 194, 168, 132, 96, 67, 53, 57, 78
        db 111, 149, 181, 201, 203, 187, 157, 120, 85, 60, 52, 62, 88, 124, 161, 189
        db 203, 199, 178, 144, 107, 75, 55, 53, 69, 100, 137, 172, 196, 204, 194, 167
        db 132, 95, 66, 52, 57, 79, 112, 150, 182, 201, 203, 186, 156, 119, 84, 59
        db 52, 63, 89, 125, 162, 190, 204, 199, 177, 143, 106, 74, 55, 54, 70, 101
        db 138, 173, 197, 204, 193, 166, 130, 94, 65, 52, 57, 80, 114, 151, 183, 201
        db 202, 185, 154, 117, 83, 59, 52, 63, 90, 126, 163, 191, 204, 198, 176, 142
        db 105, 73, 55, 54, 71, 102, 139, 174, 197, 204, 193, 166, 130, 93, 65, 52
        db 58, 80, 114, 151, 183, 201, 202, 185, 154, 117, 83, 59, 52, 63, 90, 126
        db 162, 191, 204, 199, 177, 143, 106, 74, 55, 53, 70, 100, 138, 172, 196, 204
        db 194, 167, 132, 95, 66, 52, 57, 78, 111, 149, 181, 201, 203, 187, 157, 121
        db 85, 61, 52, 61, 87, 122, 159, 188, 203, 200, 180, 147, 110, 77, 56, 53
        db 67, 96, 132, 168, 194, 204, 196, 172, 138, 101, 70, 54, 55, 73, 105, 142
        db 175, 198, 204, 192, 165, 129, 92, 65, 52, 58, 80, 113, 150, 182, 201, 203
        db 187, 157, 120, 85, 61, 52, 61, 86, 121, 158, 187, 203, 201, 181, 150, 113
        db 79, 57, 52, 65, 93, 128, 164, 192, 204, 198, 176, 143, 106, 74, 55, 53
        db 69, 98, 135, 170, 195, 204, 196, 171, 137, 100, 70, 54, 54, 73, 104, 140
        db 174, 197, 204, 193, 167, 132, 96, 67, 53, 56, 76, 108, 145, 177, 199, 204
        db 191, 164, 128, 92, 65, 52, 57, 78, 111, 148, 180, 200, 203, 189, 161, 125
        db 90, 64, 52, 58, 80, 113, 150, 181, 201, 203, 188, 159, 124, 89, 63, 52
        db 59, 81, 114, 151, 182, 201, 203, 188, 159, 123, 88, 63, 52, 59, 81, 114
        db 151, 182, 201, 203, 188, 160, 124, 89, 63, 52, 58, 80, 113, 149, 181, 200
        db 203, 189, 162, 126, 91, 64, 52, 57, 78, 110, 146, 178, 199, 204, 191, 165
        db 130, 94, 67, 53, 56, 75, 106, 142, 175, 197, 204, 194, 169, 135, 99, 70
        db 54, 54, 71, 101, 136, 170, 195, 204, 197, 174, 141, 105, 74, 56, 53, 67
        db 94, 129, 164, 191, 204, 200, 180, 149, 113, 81, 59, 52, 62, 86, 120, 156
        db 185, 202, 202, 187, 158, 123, 89, 63, 52, 57, 78, 110, 145, 177, 198, 204
        db 193, 168, 134, 99, 70, 54, 54, 70, 98, 133, 167, 192, 204, 199, 179, 147
        db 112, 80, 58, 52, 62, 86, 119, 154, 184, 201, 203, 189, 161, 127, 93, 66
        db 53, 56, 74, 104, 139, 172, 195, 204, 197, 175, 144, 108, 77, 57, 52, 63
        db 88, 121, 156, 185, 201, 203, 188, 161, 127, 93, 67, 53, 55, 73, 102, 136
        db 169, 194, 204, 199, 178, 148, 113, 81, 59, 52, 60, 83, 115, 149, 180, 199
        db 204, 193, 168, 136, 101, 72, 55, 53, 66, 92, 126, 160, 187, 202, 202, 187
        db 159, 126, 92, 66, 53, 55, 72, 100, 135, 167, 192, 204, 200, 181, 152, 118
        db 86, 62, 52, 57, 77, 107, 141, 172, 195, 204, 198, 177, 147, 113, 82, 60
        db 52, 59, 80, 111, 145, 176, 197, 204, 196, 175, 144, 110, 80, 59, 52, 60
        db 81, 112, 146, 177, 197, 204, 196, 175, 144, 110, 80, 59, 52, 60, 81, 111
        db 145, 176, 197, 204, 197, 176, 146, 112, 82, 60, 52, 58, 78, 108, 142, 173
        db 195, 204, 199, 180, 150, 117, 86, 63, 52, 56, 74, 102, 136, 167, 191, 203
        db 201, 185, 157, 125, 92, 67, 53, 54, 68, 94, 127, 159, 186, 201, 203, 191
        db 166, 135, 102, 74, 56, 52, 62, 85, 115, 148, 177, 197, 204, 197, 177, 147
        db 115, 84, 62, 52, 56, 74, 101, 134, 165, 190, 203, 202, 188, 162, 130, 98
        db 72, 55, 52, 63, 86, 117, 149, 178, 197, 204, 197, 177, 149, 116, 86, 63
        db 52, 55, 71, 98, 129, 161, 186, 201, 203, 191, 168, 138, 106, 78, 58, 52
        db 59, 78, 107, 139, 169, 192, 203, 201, 187, 161, 130, 98, 72, 56, 52, 62
        db 83, 113, 145, 174, 195, 204, 200, 183, 157, 125, 94, 69, 55, 53, 64, 86
        db 116, 147, 176, 196, 204, 199, 182, 155, 124, 94, 69, 54, 53, 64, 86, 115
        db 147, 175, 195, 204, 200, 183, 157, 126, 96, 71, 55, 52, 62, 83, 112, 143
        db 172, 193, 204, 201, 186, 162, 132, 101, 75, 57, 52, 59, 78, 105, 136, 166
        db 189, 202, 203, 191, 169, 140, 109, 81, 61, 52, 56, 71, 96, 126, 156, 182
        db 199, 204, 197, 179, 152, 122, 92, 68, 55, 53, 63, 84, 112, 142, 171, 192
        db 203, 202, 189, 166, 137, 107, 80, 61, 52, 56, 71, 95, 125, 155, 181, 198
        db 204, 198, 181, 156, 126, 97, 72, 56, 52, 60, 78, 104, 134, 163, 187, 201
        db 204, 195, 175, 149, 119, 90, 68, 54, 53, 62, 83, 110, 140, 168, 190, 202
        db 203, 193, 172, 145, 115, 87, 66, 54, 53, 64, 84, 111, 141, 169, 190, 202
        db 203, 193, 172, 145, 116, 88, 66, 54, 53, 63, 83, 109, 139, 167, 189, 202
        db 204, 194, 175, 149, 120, 92, 69, 55, 52, 60, 78, 104, 132, 161, 184, 199
        db 204, 198, 181, 157, 128, 100, 75, 58, 52, 56, 71, 94, 122, 151, 177, 195
        db 204, 202, 189, 167, 140, 111, 85, 65, 53, 53, 63, 83, 109, 137, 165, 187
        db 201, 204, 197, 180, 156, 127, 99, 75, 59, 52, 56, 70, 92, 119, 148, 173
        db 193, 203, 203, 192, 173, 147, 119, 92, 70, 56, 52, 58, 75, 98, 126, 153
        db 178, 195, 204, 202, 190, 169, 143, 115, 88, 68, 55, 52, 60, 76, 100, 128
        db 155, 179, 196, 204, 201, 189, 169, 143, 115, 89, 68, 55, 52, 59, 75, 98
        db 126, 153, 177, 195, 203, 202, 191, 172, 147, 119, 93, 71, 57, 52, 57, 71
        db 93, 119, 147, 172, 191, 202, 204, 195, 178, 155, 128, 101, 77, 61, 52, 54
        db 65, 84, 109, 136, 162, 184, 199, 204, 200, 187, 166, 140, 113, 88, 68, 55
        db 52, 58, 73, 95, 121, 148, 172, 191, 202, 204, 196, 179, 157, 130, 104, 80
        db 63, 53, 53, 62, 79, 103, 129, 155, 178, 195, 203, 203, 193, 175, 151, 125
        db 99, 76, 60, 52, 54, 64, 82, 106, 133, 158, 180, 196, 204, 202, 192, 173
        db 150, 123, 98, 76, 60, 52, 54, 64, 82, 105, 131, 157, 179, 195, 203, 203
        db 193, 176, 153, 127, 101, 78, 62, 53, 53, 62, 78, 101, 126, 152, 175, 192
        db 202, 204, 196, 181, 159, 134, 108, 85, 66, 55, 52, 58, 72, 92, 116, 142
        db 166, 186, 199, 204, 200, 188, 169, 146, 120, 95, 74, 59, 52, 54, 64, 81
        db 103, 128, 153, 176, 193, 202, 204, 196, 182, 161, 136, 111, 87, 68, 56, 52
        db 56, 68, 87, 110, 136, 160, 181, 196, 203, 203, 194, 177, 156, 131, 106, 83
        db 66, 55, 52, 57, 70, 90, 113, 138, 162, 183, 197, 204, 202, 193, 177, 155
        db 130, 106, 83, 66, 55, 52, 57, 70, 89, 112, 137, 161, 181, 196, 203, 203
        db 194, 179, 158, 134, 109, 86, 68, 56, 52, 55, 67, 85, 107, 131, 156, 177
        db 193, 202, 204, 197, 184, 164, 141, 117, 93, 73, 59, 52, 53, 62, 77, 98
        db 122, 146, 169, 187, 199, 204, 201, 191, 174, 152, 128, 104, 83, 66, 55, 52
        db 56, 68, 86, 108, 133, 156, 177, 193, 202, 204, 198, 185, 166, 144, 120, 96
        db 76, 61, 53, 52, 59, 73, 92, 115, 139, 162, 182, 196, 203, 203, 196, 181
        db 162, 139, 115, 92, 73, 59, 52, 53, 61, 75, 95, 118, 142, 164, 183, 197
        db 204, 203, 195, 180, 161, 138, 114, 92, 73, 59, 52, 53, 61, 75, 95, 117
        db 141, 163, 182, 196, 203, 203, 196, 182, 163, 141, 117, 94, 75, 61, 53, 52
        db 59, 72, 91, 113, 136, 159, 179, 194, 202, 204, 198, 186, 168, 147, 123, 100
        db 80, 64, 55, 52, 56, 67, 84, 105, 128, 151, 172, 189, 200, 204, 201, 191
        db 176, 156, 133, 110, 88, 70, 58, 52, 53, 61, 76, 95, 117, 140, 162, 181
        db 195, 203, 204, 197, 185, 167, 145, 122, 100, 80, 64, 55, 52, 56, 67, 83
        db 103, 126, 149, 170, 187, 199, 204, 202, 194, 179, 160, 138, 115, 93, 75, 61
        db 53, 52, 58, 71, 88, 109, 132, 154, 174, 190, 201, 204, 201, 191, 176, 156
        db 134, 111, 90, 72, 59, 53, 53, 60, 73, 91, 112, 135, 157, 177, 192, 201
        db 204, 200, 190, 174, 154, 132, 109, 88, 71, 59, 52, 53, 60, 74, 92, 113
        db 136, 158, 177, 192, 201, 204, 200, 190, 174, 154, 132, 110, 89, 71, 59, 53
        db 53, 60, 73, 90, 111, 134, 156, 175, 191, 201, 204, 201, 191, 176, 157, 135
        db 113, 91, 73, 60, 53, 52, 58, 70, 87, 108, 130, 152, 172, 188, 199, 204
        db 202, 194, 180, 161, 140, 117, 96, 77, 63, 54, 52, 56, 67, 83, 103, 125
        db 147, 168, 185, 197, 203, 203, 197, 184, 167, 146, 123, 102, 82, 66, 56, 52
        db 54, 63, 77, 96, 118, 140, 161, 180, 194, 202, 204, 199, 189, 173, 153, 131
        db 109, 88, 71, 59, 53, 53, 59, 72, 89, 110, 132, 154, 173, 189, 200, 204
        db 202, 194, 179, 161, 140, 117, 96, 77, 63, 54, 52, 56, 66, 82, 101, 123
        db 145, 166, 183, 196, 203, 204, 198, 186, 169, 149, 127, 105, 85, 69, 57, 52
        db 53, 61, 74, 92, 113, 135, 157, 176, 191, 201, 204, 201, 192, 177, 159, 137
        db 115, 94, 76, 62, 54, 52, 57, 67, 83, 103, 125, 147, 168, 185, 197, 203
smpfrt: db 180, 195, 193, 155, 98, 62, 62, 77, 75, 61, 65, 106, 163, 196, 192, 178
        db 183, 197, 186, 138, 83, 58, 68, 79, 68, 58, 83, 140, 188, 196, 181, 180
        db 195, 190, 143, 83, 58, 70, 78, 65, 61, 101, 165, 197, 188, 178, 191, 195
        db 151, 86, 58, 71, 77, 61, 70, 128, 187, 195, 178, 187, 197, 156, 86, 58
        db 74, 74, 58, 89, 162, 198, 183, 182, 198, 162, 87, 58, 76, 70, 61, 116
        db 187, 192, 178, 195, 177, 97, 58, 76, 68, 64, 134, 196, 184, 183, 196, 133
        db 63, 71, 73, 60, 126, 195, 183, 186, 192, 113, 58, 77, 63, 80, 171, 194
        db 178, 198, 138, 61, 75, 66, 77, 171, 193, 180, 196, 118, 58, 79, 59, 110
        db 195, 180, 193, 165, 68, 72, 66, 82, 183, 186, 188, 177, 75, 70, 68, 82
        db 185, 184, 191, 166, 65, 75, 61, 106, 196, 178, 198, 127, 58, 78, 61, 158
        db 192, 185, 179, 71, 74, 62, 110, 198, 177, 197, 103, 64, 71, 79, 188, 180
        db 197, 131, 59, 77, 66, 175, 185, 193, 149, 58, 78, 61, 164, 188, 190, 159
        db 60, 79, 59, 157, 190, 188, 165, 61, 78, 58, 152, 192, 186, 170, 63, 78
        db 58, 145, 194, 184, 175, 66, 77, 58, 135, 196, 182, 184, 73, 74, 60, 118
        db 198, 178, 193, 88, 68, 66, 93, 195, 178, 198, 119, 60, 76, 67, 173, 187
        db 190, 165, 63, 77, 59, 121, 198, 178, 197, 110, 61, 76, 64, 164, 192, 184
        db 185, 82, 67, 70, 75, 177, 188, 186, 181, 80, 67, 71, 70, 169, 192, 181
        db 193, 103, 60, 78, 58, 127, 197, 178, 195, 162, 70, 69, 72, 65, 152, 197
        db 177, 197, 158, 70, 68, 74, 60, 132, 197, 181, 189, 187, 105, 58, 77, 64
        db 74, 157, 198, 179, 191, 186, 107, 58, 75, 70, 62, 126, 193, 188, 179, 198
        db 164, 86, 59, 77, 68, 62, 120, 188, 193, 177, 193, 188, 124, 65, 65, 79
        db 63, 66, 125, 187, 194, 178, 188, 196, 154, 86, 58, 73, 76, 60, 72, 131
        db 187, 195, 179, 183, 198, 177, 116, 66, 61, 77, 73, 58, 76, 132, 185, 197
        db 182, 180, 195, 191, 146, 87, 59, 67, 79, 69, 58, 80, 135, 185, 197, 184
        db 178, 191, 196, 165, 108, 65, 60, 75, 77, 62, 62, 98, 155, 193, 195, 180
        db 180, 195, 193, 155, 98, 62, 62, 77, 75, 61, 65, 106, 163, 196, 192, 178
        db 183, 197, 186, 138, 83, 58, 68, 79, 68, 58, 83, 140, 188, 196, 181, 180
        db 195, 190, 143, 83, 58, 70, 78, 65, 61, 101, 165, 197, 188, 178, 191, 195
        db 151, 86, 58, 71, 77, 61, 70, 128, 187, 195, 178, 187, 197, 156, 86, 58
        db 74, 74, 58, 89, 162, 198, 183, 182, 198, 162, 87, 58, 76, 70, 61, 116
        db 187, 192, 178, 195, 177, 97, 58, 76, 68, 64, 134, 196, 184, 183, 196, 133
        db 63, 71, 73, 60, 126, 195, 183, 186, 192, 113, 58, 77, 63, 80, 171, 194
        db 178, 198, 138, 61, 75, 66, 77, 171, 193, 180, 196, 118, 58, 79, 59, 110
        db 195, 180, 193, 165, 68, 72, 66, 82, 183, 186, 188, 177, 75, 70, 68, 82
        db 185, 184, 191, 166, 65, 75, 61, 106, 196, 178, 198, 127, 58, 78, 61, 158
        db 192, 185, 179, 71, 74, 62, 110, 198, 177, 197, 103, 64, 71, 79, 188, 180
        db 197, 131, 59, 77, 66, 175, 185, 193, 149, 58, 78, 61, 164, 188, 190, 159
        db 60, 79, 59, 157, 190, 188, 165, 61, 78, 58, 152, 192, 186, 170, 63, 78
        db 58, 145, 194, 184, 175, 66, 77, 58, 135, 196, 182, 184, 73, 74, 60, 118
        db 198, 178, 193, 88, 68, 66, 93, 195, 178, 198, 119, 60, 76, 67, 173, 187
        db 190, 165, 63, 77, 59, 121, 198, 178, 197, 110, 61, 76, 64, 164, 192, 184
        db 185, 82, 67, 70, 75, 177, 188, 186, 181, 80, 67, 71, 70, 169, 192, 181
        db 193, 103, 60, 78, 58, 127, 197, 178, 195, 162, 70, 69, 72, 65, 152, 197
        db 177, 197, 158, 70, 68, 74, 60, 132, 197, 181, 189, 187, 105, 58, 77, 64
        db 74, 157, 198, 179, 191, 186, 107, 58, 75, 70, 62, 126, 193, 188, 179, 198
        db 164, 86, 59, 77, 68, 62, 120, 188, 193, 177, 193, 188, 124, 65, 65, 79
        db 63, 66, 125, 187, 194, 178, 188, 196, 154, 86, 58, 73, 76, 60, 72, 131
        db 187, 195, 179, 183, 198, 177, 116, 66, 61, 77, 73, 58, 76, 132, 185, 197
        db 182, 180, 195, 191, 146, 87, 59, 67, 79, 69, 58, 80, 135, 185, 197, 184
        db 178, 191, 196, 165, 108, 65, 60, 75, 77, 62, 62, 98, 155, 193, 195, 180
        db 180, 195, 193, 155, 98, 62, 62, 77, 75, 61, 65, 106, 163, 196, 192, 178
        db 183, 197, 186, 138, 83, 58, 68, 79, 68, 58, 83, 140, 188, 196, 181, 180
        db 195, 190, 143, 83, 58, 70, 78, 65, 61, 101, 165, 197, 188, 178, 191, 195
        db 151, 86, 58, 71, 77, 61, 70, 128, 187, 195, 178, 187, 197, 156, 86, 58
        db 74, 74, 58, 89, 162, 198, 183, 182, 198, 162, 87, 58, 76, 70, 61, 116
        db 187, 192, 178, 195, 177, 97, 58, 76, 68, 64, 134, 196, 184, 183, 196, 133
        db 63, 71, 73, 60, 126, 195, 183, 186, 192, 113, 58, 77, 63, 80, 171, 194
        db 178, 198, 138, 61, 75, 66, 77, 171, 193, 180, 196, 118, 58, 79, 59, 110
        db 195, 180, 193, 165, 68, 72, 66, 82, 183, 186, 188, 177, 75, 70, 68, 82
        db 185, 184, 191, 166, 65, 75, 61, 106, 196, 178, 198, 127, 58, 78, 61, 158
        db 192, 185, 179, 71, 74, 62, 110, 198, 177, 197, 103, 64, 71, 79, 188, 180
        db 197, 131, 59, 77, 66, 175, 185, 193, 149, 58, 78, 61, 164, 188, 190, 159
        db 60, 79, 59, 157, 190, 188, 165, 61, 78, 58, 152, 192, 186, 170, 63, 78
        db 58, 145, 194, 184, 175, 66, 77, 58, 135, 196, 182, 184, 73, 74, 60, 118
        db 198, 178, 193, 88, 68, 66, 93, 195, 178, 198, 119, 60, 76, 67, 173, 187
        db 190, 165, 63, 77, 59, 121, 198, 178, 197, 110, 61, 76, 64, 164, 192, 184
        db 185, 82, 67, 70, 75, 177, 188, 186, 181, 80, 67, 71, 70, 169, 192, 181
        db 193, 103, 60, 78, 58, 127, 197, 178, 195, 162, 70, 69, 72, 65, 152, 197
        db 177, 197, 158, 70, 68, 74, 60, 132, 197, 181, 189, 187, 105, 58, 77, 64
        db 74, 157, 198, 179, 191, 186, 107, 58, 75, 70, 62, 126, 193, 188, 179, 198
        db 164, 86, 59, 77, 68, 62, 120, 188, 193, 177, 193, 188, 124, 65, 65, 79
        db 63, 66, 125, 187, 194, 178, 188, 196, 154, 86, 58, 73, 76, 60, 72, 131
        db 187, 195, 179, 183, 198, 177, 116, 66, 61, 77, 73, 58, 76, 132, 185, 197
        db 182, 180, 195, 191, 146, 87, 59, 67, 79, 69, 58, 80, 135, 185, 197, 184
        db 178, 191, 196, 165, 108, 65, 60, 75, 77, 62, 62, 98, 155, 193, 195, 180
        db 180, 195, 193, 155, 98, 62, 62, 77, 75, 61, 65, 106, 163, 196, 192, 178
        db 183, 197, 186, 138, 83, 58, 68, 79, 68, 58, 83, 140, 188, 196, 181, 180
        db 195, 190, 143, 83, 58, 70, 78, 65, 61, 101, 165, 197, 188, 178, 191, 195
        db 151, 86, 58, 71, 77, 61, 70, 128, 187, 195, 178, 187, 197, 156, 86, 58
        db 74, 74, 58, 89, 162, 198, 183, 182, 198, 162, 87, 58, 76, 70, 61, 116
        db 187, 192, 178, 195, 177, 97, 58, 76, 68, 64, 134, 196, 184, 183, 196, 133
        db 63, 71, 73, 60, 126, 195, 183, 186, 192, 113, 58, 77, 63, 80, 171, 194
        db 178, 198, 138, 61, 75, 66, 77, 171, 193, 180, 196, 118, 58, 79, 59, 110
        db 195, 180, 193, 165, 68, 72, 66, 82, 183, 186, 188, 177, 75, 70, 68, 82
        db 185, 184, 191, 166, 65, 75, 61, 106, 196, 178, 198, 127, 58, 78, 61, 158
        db 192, 185, 179, 71, 74, 62, 110, 198, 177, 197, 103, 64, 71, 79, 188, 180
        db 197, 131, 59, 77, 66, 175, 185, 193, 149, 58, 78, 61, 164, 188, 190, 159
        db 60, 79, 59, 157, 190, 188, 165, 61, 78, 58, 152, 192, 186, 170, 63, 78
        db 58, 145, 194, 184, 175, 66, 77, 58, 135, 196, 182, 184, 73, 74, 60, 118
        db 198, 178, 193, 88, 68, 66, 93, 195, 178, 198, 119, 60, 76, 67, 173, 187
        db 190, 165, 63, 77, 59, 121, 198, 178, 197, 110, 61, 76, 64, 164, 192, 184
        db 185, 82, 67, 70, 75, 177, 188, 186, 181, 80, 67, 71, 70, 169, 192, 181
        db 193, 103, 60, 78, 58, 127, 197, 178, 195, 162, 70, 69, 72, 65, 152, 197
        db 177, 197, 158, 70, 68, 74, 60, 132, 197, 181, 189, 187, 105, 58, 77, 64
        db 74, 157, 198, 179, 191, 186, 107, 58, 75, 70, 62, 126, 193, 188, 179, 198
        db 164, 86, 59, 77, 68, 62, 120, 188, 193, 177, 193, 188, 124, 65, 65, 79
        db 63, 66, 125, 187, 194, 178, 188, 196, 154, 86, 58, 73, 76, 60, 72, 131
        db 187, 195, 179, 183, 198, 177, 116, 66, 61, 77, 73, 58, 76, 132, 185, 197
        db 182, 180, 195, 191, 146, 87, 59, 67, 79, 69, 58, 80, 135, 185, 197, 184
        db 178, 191, 196, 165, 108, 65, 60, 75, 77, 62, 62, 98, 155, 193, 195, 180
smpeye: db 83, 144, 181, 157, 128, 99, 75, 112, 173, 172, 139, 114, 81, 88, 154, 180
        db 150, 122, 90, 78, 137, 181, 157, 127, 95, 76, 132, 181, 157, 126, 92, 79
        db 143, 181, 148, 118, 81, 94, 168, 171, 134, 101, 76, 138, 181, 145, 112, 76
        db 119, 181, 151, 117, 77, 115, 181, 150, 114, 75, 127, 181, 142, 105, 76, 153
        db 172, 129, 87, 97, 179, 151, 112, 75, 147, 173, 128, 84, 108, 181, 142, 100
        db 84, 173, 155, 113, 75, 155, 167, 122, 76, 135, 176, 129, 82, 118, 180, 134
        db 89, 104, 181, 139, 95, 92, 179, 145, 102, 84, 174, 152, 109, 77, 165, 160
        db 117, 75, 149, 170, 124, 78, 127, 179, 133, 89, 100, 181, 145, 105, 79, 164
        db 164, 122, 77, 124, 180, 138, 99, 82, 167, 164, 124, 81, 108, 181, 149, 113
        db 75, 129, 181, 143, 108, 75, 135, 181, 143, 110, 75, 125, 181, 150, 118, 79
        db 100, 174, 166, 130, 96, 77, 139, 181, 149, 120, 84, 88, 158, 178, 143, 115
        db 80, 91, 160, 178, 144, 117, 83, 85, 150, 181, 152, 124, 92, 76, 127, 179
        db 165, 133, 107, 76, 99, 164, 178, 146, 120, 88, 78, 133, 180, 162, 132, 105
        db 76, 102, 167, 176, 143, 117, 84, 84, 147, 181, 153, 124, 92, 77, 133, 181
        db 158, 127, 95, 76, 133, 181, 155, 124, 89, 81, 150, 179, 144, 114, 77, 105
        db 176, 163, 128, 91, 82, 157, 175, 136, 101, 76, 145, 178, 138, 103, 76, 148
        db 177, 135, 97, 81, 163, 168, 127, 84, 102, 180, 150, 112, 75, 145, 175, 130
        db 87, 101, 180, 146, 106, 78, 163, 163, 120, 76, 137, 176, 129, 83, 112, 181
        db 137, 93, 95, 180, 144, 102, 84, 175, 151, 109, 78, 167, 158, 115, 75, 156
        db 165, 120, 75, 143, 172, 126, 79, 127, 178, 132, 86, 107, 181, 140, 97, 88
        db 176, 151, 111, 76, 156, 168, 124, 79, 120, 181, 139, 98, 83, 169, 162, 121
        db 78, 119, 181, 143, 106, 76, 148, 176, 134, 96, 82, 161, 171, 131, 93, 83
        db 161, 173, 133, 98, 77, 146, 179, 142, 110, 75, 115, 179, 159, 126, 91, 80
        db 148, 180, 146, 118, 82, 90, 160, 177, 143, 115, 81, 90, 158, 179, 146, 119
        db 86, 82, 144, 181, 155, 127, 97, 75, 118, 176, 169, 137, 111, 79, 90, 155
        db 180, 151, 124, 94, 76, 122, 178, 168, 135, 109, 78, 94, 161, 179, 147, 120
        db 87, 80, 141, 181, 155, 126, 94, 76, 132, 181, 158, 127, 94, 77, 137, 181
        db 152, 122, 86, 86, 159, 176, 139, 108, 75, 120, 181, 154, 121, 82, 97, 173
        db 164, 127, 88, 89, 169, 166, 127, 87, 93, 174, 160, 122, 79, 111, 181, 147
        db 110, 75, 147, 175, 131, 88, 97, 179, 150, 110, 75, 154, 169, 125, 79, 120
        db 180, 136, 92, 95, 180, 146, 104, 81, 171, 156, 113, 75, 158, 165, 120, 75
        db 144, 172, 125, 78, 130, 177, 130, 83, 116, 180, 134, 89, 102, 181, 140, 97
        db 89, 178, 149, 107, 78, 166, 160, 117, 75, 144, 173, 128, 82, 111, 181, 141
        db 101, 82, 168, 161, 121, 77, 124, 181, 140, 101, 79, 160, 169, 128, 86, 95
        db 175, 159, 122, 80, 105, 179, 156, 121, 81, 102, 176, 161, 125, 87, 87, 163
        db 173, 135, 103, 75, 128, 181, 154, 122, 87, 84, 154, 179, 144, 116, 81, 91
        db 161, 177, 143, 116, 82, 88, 155, 180, 148, 121, 89, 79, 136, 181, 160, 130
        db 102, 75, 108, 171, 174, 141, 116, 83, 83, 144, 181, 157, 128, 99, 75, 112
        db 173, 172, 139, 114, 81, 88, 154, 180, 150, 122, 90, 78, 137, 181, 157, 127
        db 95, 76, 132, 181, 157, 126, 92, 79, 143, 181, 148, 118, 81, 94, 168, 171
        db 134, 101, 76, 138, 181, 145, 112, 76, 119, 181, 151, 117, 77, 115, 181, 150
        db 114, 75, 127, 181, 142, 105, 76, 153, 172, 129, 87, 97, 179, 151, 112, 75
        db 147, 173, 128, 84, 108, 181, 142, 100, 84, 173, 155, 113, 75, 155, 167, 122
        db 76, 135, 176, 129, 82, 118, 180, 134, 89, 104, 181, 139, 95, 92, 179, 145
        db 102, 84, 174, 152, 109, 77, 165, 160, 117, 75, 149, 170, 124, 78, 127, 179
        db 133, 89, 100, 181, 145, 105, 79, 164, 164, 122, 77, 124, 180, 138, 99, 82
        db 167, 164, 124, 81, 108, 181, 149, 113, 75, 129, 181, 143, 108, 75, 135, 181
        db 143, 110, 75, 125, 181, 150, 118, 79, 100, 174, 166, 130, 96, 77, 139, 181
        db 149, 120, 84, 88, 158, 178, 143, 115, 80, 91, 160, 178, 144, 117, 83, 85
        db 150, 181, 152, 124, 92, 76, 127, 179, 165, 133, 107, 76, 99, 164, 178, 146
        db 120, 88, 78, 133, 180, 162, 132, 105, 76, 102, 167, 176, 143, 117, 84, 84
        db 147, 181, 153, 124, 92, 77, 133, 181, 158, 127, 95, 76, 133, 181, 155, 124
        db 89, 81, 150, 179, 144, 114, 77, 105, 176, 163, 128, 91, 82, 157, 175, 136
        db 101, 76, 145, 178, 138, 103, 76, 148, 177, 135, 97, 81, 163, 168, 127, 84
        db 102, 180, 150, 112, 75, 145, 175, 130, 87, 101, 180, 146, 106, 78, 163, 163
        db 120, 76, 137, 176, 129, 83, 112, 181, 137, 93, 95, 180, 144, 102, 84, 175
        db 151, 109, 78, 167, 158, 115, 75, 156, 165, 120, 75, 143, 172, 126, 79, 127
        db 178, 132, 86, 107, 181, 140, 97, 88, 176, 151, 111, 76, 156, 168, 124, 79
        db 120, 181, 139, 98, 83, 169, 162, 121, 78, 119, 181, 143, 106, 76, 148, 176
        db 134, 96, 82, 161, 171, 131, 93, 83, 161, 173, 133, 98, 77, 146, 179, 142
        db 110, 75, 115, 179, 159, 126, 91, 80, 148, 180, 146, 118, 82, 90, 160, 177
        db 143, 115, 81, 90, 158, 179, 146, 119, 86, 82, 144, 181, 155, 127, 97, 75
        db 118, 176, 169, 137, 111, 79, 90, 155, 180, 151, 124, 94, 76, 122, 178, 168
        db 135, 109, 78, 94, 161, 179, 147, 120, 87, 80, 141, 181, 155, 126, 94, 76
        db 132, 181, 158, 127, 94, 77, 137, 181, 152, 122, 86, 86, 159, 176, 139, 108
        db 75, 120, 181, 154, 121, 82, 97, 173, 164, 127, 88, 89, 169, 166, 127, 87
        db 93, 174, 160, 122, 79, 111, 181, 147, 110, 75, 147, 175, 131, 88, 97, 179
        db 150, 110, 75, 154, 169, 125, 79, 120, 180, 136, 92, 95, 180, 146, 104, 81
        db 171, 156, 113, 75, 158, 165, 120, 75, 144, 172, 125, 78, 130, 177, 130, 83
        db 116, 180, 134, 89, 102, 181, 140, 97, 89, 178, 149, 107, 78, 166, 160, 117
        db 75, 144, 173, 128, 82, 111, 181, 141, 101, 82, 168, 161, 121, 77, 124, 181
        db 140, 101, 79, 160, 169, 128, 86, 95, 175, 159, 122, 80, 105, 179, 156, 121
        db 81, 102, 176, 161, 125, 87, 87, 163, 173, 135, 103, 75, 128, 181, 154, 122
        db 87, 84, 154, 179, 144, 116, 81, 91, 161, 177, 143, 116, 82, 88, 155, 180
        db 148, 121, 89, 79, 136, 181, 160, 130, 102, 75, 108, 171, 174, 141, 116, 83
smpeat: db 128, 130, 132, 133, 134, 135, 136, 138, 142, 145, 145, 140, 131, 120, 108, 101
        db 99, 100, 103, 103, 99, 93, 87, 88, 98, 117, 141, 163, 177, 181, 178, 172
        db 170, 175, 184, 192, 191, 176, 147, 111, 78, 57, 51, 56, 65, 69, 63, 50
        db 40, 44, 67, 105, 151, 190, 212, 216, 207, 196, 191, 197, 209, 217, 211, 185
        db 145, 99, 62, 42, 41, 51, 62, 65, 57, 45, 39, 49, 79, 122, 167, 201
        db 216, 213, 202, 192, 192, 202, 214, 216, 199, 164, 118, 75, 47, 39, 46, 59
        db 65, 60, 49, 40, 45, 70, 111, 158, 195, 215, 215, 204, 193, 192, 201, 213
        db 216, 201, 166, 120, 76, 47, 39, 47, 59, 65, 60, 47, 39, 47, 74, 118
        db 165, 200, 216, 213, 201, 192, 193, 204, 215, 214, 192, 151, 104, 64, 42, 41
        db 52, 63, 64, 55, 42, 40, 58, 95, 143, 186, 212, 216, 206, 194, 191, 200
        db 212, 216, 201, 165, 118, 73, 46, 39, 49, 61, 65, 57, 44, 39, 54, 90
        db 138, 183, 211, 217, 207, 195, 191, 200, 212, 216, 200, 163, 114, 70, 44, 40
        db 51, 62, 64, 55, 42, 41, 60, 100, 150, 192, 214, 215, 203, 192, 193, 204
        db 216, 213, 188, 144, 94, 56, 40, 44, 57, 65, 61, 48, 39, 48, 81, 129
        db 176, 208, 217, 208, 195, 191, 200, 213, 216, 197, 157, 106, 63, 41, 42, 54
        db 64, 62, 50, 40, 46, 76, 124, 173, 207, 217, 208, 195, 191, 200, 213, 216
        db 196, 154, 103, 60, 40, 43, 56, 65, 61, 47, 39, 50, 85, 136, 183, 212
        db 216, 204, 193, 193, 204, 216, 211, 182, 134, 84, 49, 39, 48, 61, 65, 55
        db 42, 42, 66, 112, 164, 202, 217, 210, 196, 191, 199, 213, 216, 194, 150, 98
        db 57, 39, 45, 59, 65, 58, 44, 40, 60, 104, 157, 199, 216, 211, 197, 191
        db 199, 212, 216, 195, 151, 98, 56, 39, 45, 59, 65, 56, 43, 41, 64, 111
        db 164, 203, 217, 209, 195, 191, 202, 215, 213, 185, 135, 83, 48, 39, 50, 63
        db 63, 51, 40, 47, 82, 134, 184, 213, 215, 202, 191, 195, 210, 217, 201, 159
        db 104, 59, 40, 45, 59, 65, 56, 42, 42, 69, 118, 172, 208, 216, 205, 193
        db 193, 207, 217, 206, 167, 112, 64, 41, 44, 58, 65, 57, 43, 41, 68, 118
        db 172, 208, 216, 205, 192, 194, 208, 217, 203, 161, 105, 59, 40, 46, 60, 65
        db 54, 40, 45, 78, 132, 184, 213, 214, 200, 191, 198, 213, 215, 190, 140, 84
        db 47, 40, 52, 64, 61, 47, 39, 58, 104, 161, 204, 217, 207, 193, 193, 207
        db 217, 203, 160, 103, 57, 39, 47, 62, 64, 51, 39, 50, 91, 149, 197, 217
        db 210, 195, 192, 204, 217, 207, 166, 109, 60, 39, 46, 61, 64, 51, 39, 50
        db 92, 150, 198, 217, 209, 194, 192, 206, 217, 203, 159, 100, 54, 39, 49, 63
        db 62, 48, 39, 57, 106, 165, 206, 216, 204, 192, 196, 211, 216, 190, 137, 80
        db 44, 41, 56, 65, 57, 42, 44, 78, 135, 189, 216, 211, 196, 192, 204, 217
        db 205, 161, 101, 54, 39, 50, 64, 61, 46, 40, 65, 118, 177, 212, 214, 199
        db 191, 201, 215, 210, 170, 110, 59, 39, 48, 63, 62, 47, 39, 62, 115, 175
        db 212, 215, 199, 191, 201, 216, 209, 167, 106, 56, 39, 50, 64, 61, 45, 41
        db 69, 125, 184, 215, 212, 196, 192, 205, 217, 201, 152, 90, 47, 40, 55, 65
        db 56, 41, 47, 88, 150, 200, 217, 205, 192, 196, 212, 214, 181, 121, 64, 40
        db 47, 63, 62, 47, 40, 66, 124, 184, 215, 211, 195, 192, 207, 217, 195, 140
        db 79, 43, 43, 59, 64, 51, 39, 57, 111, 173, 212, 214, 197, 191, 205, 217
        db 200, 147, 84, 44, 42, 59, 65, 51, 39, 57, 110, 174, 212, 213, 197, 192
        db 206, 217, 197, 141, 78, 42, 44, 61, 64, 48, 39, 64, 123, 185, 215, 210
        db 194, 194, 210, 215, 184, 122, 63, 39, 49, 64, 60, 43, 44, 84, 149, 202
        db 217, 203, 191, 200, 216, 206, 156, 90, 46, 42, 58, 65, 50, 39, 62, 120
        db 184, 215, 210, 193, 195, 212, 214, 177, 111, 56, 39, 53, 65, 55, 40, 52
        db 104, 171, 212, 213, 196, 193, 209, 216, 185, 122, 62, 39, 51, 65, 57, 41
        db 49, 100, 167, 211, 213, 196, 192, 209, 216, 184, 120, 60, 39, 52, 65, 56
        db 40, 53, 107, 175, 214, 211, 194, 194, 212, 213, 174, 105, 52, 40, 56, 65
        db 51, 39, 64, 127, 191, 217, 206, 191, 199, 216, 204, 150, 81, 42, 45, 63
        db 61, 43, 45, 89, 159, 209, 214, 197, 192, 209, 215, 180, 111, 54, 40, 56
        db 65, 50, 39, 67, 133, 195, 217, 203, 191, 203, 217, 195, 132, 67, 39, 51
        db 65, 55, 39, 57, 117, 185, 216, 206, 191, 200, 216, 201, 142, 73, 40, 49
        db 65, 57, 40, 54, 113, 182, 216, 207, 191, 199, 216, 201, 141, 72, 40, 50
        db 65, 56, 39, 58, 120, 189, 217, 204, 191, 202, 217, 194, 128, 62, 39, 54
        db 65, 51, 39, 70, 139, 201, 216, 199, 192, 208, 215, 176, 103, 48, 42, 61
        db 62, 44, 45, 95, 169, 213, 210, 192, 197, 216, 203, 142, 71, 39, 51, 65
        db 53, 39, 67, 136, 200, 216, 198, 192, 210, 214, 169, 95, 45, 44, 63, 59
        db 41, 52, 113, 185, 217, 204, 191, 204, 217, 185, 112, 51, 41, 60, 62, 43
        db 46, 101, 176, 215, 207, 191, 202, 217, 190, 119, 55, 40, 59, 63, 44, 45
        db 99, 175, 215, 207, 191, 202, 217, 188, 115, 52, 41, 60, 62, 43, 49, 107
        db 183, 217, 204, 191, 206, 216, 177, 101, 46, 44, 63, 58, 40, 58, 127, 197
        db 216, 198, 193, 212, 210, 155, 78, 40, 51, 65, 51, 40, 80, 158, 211, 211
        db 192, 199, 217, 192, 119, 53, 41, 61, 61, 41, 53, 118, 192, 217, 199, 192
        db 211, 210, 153, 75, 39, 53, 65, 48, 42, 90, 169, 215, 207, 191, 204, 216
        db 176, 96, 43, 47, 65, 54, 39, 73, 152, 210, 211, 192, 200, 217, 188, 110
        db 48, 44, 63, 57, 39, 66, 143, 207, 213, 193, 198, 217, 191, 114, 50, 43
        db 63, 57, 39, 66, 143, 207, 212, 193, 199, 217, 187, 109, 47, 45, 64, 55
        db 39, 73, 154, 212, 210, 191, 203, 216, 175, 93, 42, 49, 65, 50, 41, 91
        db 173, 216, 203, 191, 209, 211, 152, 71, 39, 57, 63, 43, 51, 120, 197, 216
        db 195, 196, 216, 194, 116, 49, 44, 64, 55, 39, 77, 161, 214, 206, 191, 207
        db 213, 157, 74, 39, 56, 63, 42, 53, 125, 200, 215, 194, 198, 217, 185, 102
        db 43, 48, 65, 49, 42, 98, 182, 217, 199, 193, 214, 200, 124, 52, 43, 64
        db 55, 39, 82, 168, 216, 203, 192, 211, 207, 137, 58, 41, 62, 57, 39, 74
        db 160, 215, 205, 191, 210, 209, 141, 60, 41, 62, 58, 39, 75, 161, 215, 204
        db 191, 211, 207, 136, 57, 42, 63, 56, 39, 83, 171, 216, 201, 193, 214, 200
        db 121, 49, 45, 65, 51, 42, 100, 187, 217, 196, 197, 217, 184, 97, 41, 52
        db 64, 44, 52, 129, 205, 212, 191, 205, 214, 156, 68, 40, 61, 58, 39, 77
        db 166, 216, 201, 193, 215, 197, 113, 45, 48, 65, 46, 48, 121, 201, 213, 192
        db 204, 214, 155, 67, 40, 62, 57, 39, 85, 176, 217, 198, 195, 217, 184, 94
        db 40, 54, 63, 41, 62, 149, 213, 205, 191, 213, 201, 118, 46, 48, 65, 45
        db 50, 129, 206, 210, 191, 209, 209, 134, 53, 44, 65, 49, 45, 117, 201, 213
        db 191, 206, 212, 142, 56, 43, 65, 50, 44, 114, 199, 213, 191, 206, 212, 141
        db 56, 44, 65, 49, 46, 119, 203, 211, 191, 208, 208, 131, 50, 46, 65, 46
        db 51, 133, 209, 208, 191, 212, 200, 113, 43, 51, 64, 41, 64, 155, 215, 201
        db 194, 216, 182, 87, 39, 59, 59, 39, 88, 183, 216, 194, 202, 215, 151, 60
        db 43, 65, 49, 47, 126, 207, 208, 191, 213, 198, 107, 41, 54, 62, 40, 76
        db 173, 217, 195, 200, 216, 156, 62, 42, 65, 49, 48, 130, 209, 207, 192, 215
        db 191, 95, 39, 58, 59, 39, 93, 189, 215, 192, 206, 209, 128, 47, 50, 64
        db 41, 68, 165, 217, 196, 199, 216, 154, 59, 44, 65, 46, 54, 145, 214, 201
        db 195, 217, 170, 71, 41, 64, 50, 47, 131, 211, 205, 193, 216, 179, 79, 40
        db 63, 52, 45, 125, 209, 206, 192, 216, 181, 80, 40, 63, 52, 45, 127, 210
        db 205, 193, 216, 176, 75, 40, 64, 49, 49, 137, 213, 202, 195, 217, 164, 64
        db 43, 65, 45, 58, 155, 216, 197, 199, 215, 144, 51, 48, 64, 40, 75, 178
        db 216, 192, 207, 206, 114, 41, 56, 59, 40, 105, 201, 209, 191, 215, 183, 79
        db 40, 64, 48, 52, 146, 216, 198, 199, 215, 142, 49, 50, 63, 39, 86, 189
        db 213, 191, 212, 192, 89, 39, 63, 50, 49, 142, 215, 198, 199, 215, 139, 48
        db 51, 62, 39, 95, 197, 211, 191, 215, 180, 74, 41, 65, 45, 62, 166, 217
        db 193, 207, 204, 106, 40, 60, 54, 45, 134, 214, 199, 199, 214, 136, 46, 53
        db 60, 40, 108, 206, 206, 194, 217, 157, 55, 48, 64, 39, 91, 196, 210, 192
        db 216, 171, 64, 44, 65, 41, 80, 188, 213, 191, 215, 178, 70, 43, 65, 41
        db 77, 186, 213, 191, 215, 179, 70, 43, 65, 41, 79, 188, 212, 191, 216, 174
        db 65, 45, 65, 40, 87, 195, 210, 192, 217, 162, 56, 48, 63, 39, 103, 205
        db 205, 195, 216, 142, 46, 54, 58, 42, 127, 214, 198, 201, 210, 114, 40, 61
        db 50, 53, 158, 217, 192, 210, 192, 81, 41, 65, 42, 79, 191, 211, 192, 217
        db 157, 52, 51, 60, 40, 122, 213, 198, 202, 209, 108, 39, 63, 47, 61, 173
        db 215, 191, 215, 173, 61, 48, 63, 39, 113, 211, 200, 200, 210, 111, 39, 63
        db 47, 64, 177, 214, 191, 216, 163, 54, 51, 60, 41, 130, 215, 195, 206, 199
        db 87, 41, 65, 41, 88, 200, 206, 196, 215, 126, 40, 61, 49, 59, 174, 214
        db 191, 216, 159, 51, 54, 57, 45, 145, 217, 192, 212, 183, 66, 46, 63, 40
        db 121, 214, 196, 206, 198, 83, 42, 65, 39, 103, 209, 200, 202, 206, 95, 40
        db 65, 40, 93, 205, 202, 199, 209, 103, 39, 65, 41, 88, 202, 203, 199, 210
        db 104, 39, 65, 41, 89, 203, 203, 200, 208, 99, 40, 65, 40, 97, 207, 200
        db 202, 204, 89, 41, 65, 39, 111, 213, 196, 207, 194, 73, 45, 63, 40, 132
        db 217, 192, 212, 176, 57, 52, 57, 47, 159, 216, 191, 217, 148, 43, 60, 48
        db 66, 188, 209, 195, 213, 110, 39, 65, 40, 99, 210, 197, 206, 194, 71, 47
        db 61, 43, 146, 217, 191, 216, 152, 44, 60, 48, 69, 192, 206, 198, 209, 96
        db 41, 65, 39, 123, 216, 192, 213, 169, 50, 57, 52, 61, 184, 209, 196, 211
        db 101, 40, 65, 39, 124, 216, 192, 214, 162, 47, 59, 49, 69, 194, 205, 200
        db 204, 83, 44, 62, 42, 149, 216, 191, 217, 132, 39, 64, 41, 101, 212, 195
        db 211, 175, 52, 56, 51, 65, 192, 205, 200, 203, 79, 46, 60, 46, 162, 214
        db 193, 215, 110, 40, 65, 39, 132, 217, 191, 217, 138, 40, 64, 40, 106, 214
        db 193, 214, 160, 44, 62, 44, 88, 209, 196, 210, 175, 51, 58, 48, 76, 203
        db 199, 207, 184, 56, 56, 50, 69, 198, 201, 205, 188, 59, 55, 51, 67, 197
        db 201, 205, 188, 59, 56, 52, 70, 198, 199, 205, 183, 56, 59, 51, 78, 202
        db 195, 206, 173, 52, 63, 49, 90, 206, 191, 208, 157, 48, 68, 47, 109, 209
        db 187, 210, 136, 46, 70, 47, 133, 209, 185, 207, 109, 50, 70, 53, 161, 202
        db 189, 197, 82, 59, 64, 69, 186, 192, 196, 173, 60, 69, 56, 100, 202, 183
        db 203, 136, 53, 75, 53, 142, 201, 182, 197, 93, 61, 69, 70, 181, 188, 192
        db 169, 63, 74, 59, 111, 199, 178, 199, 119, 58, 77, 62, 162, 191, 184, 180
        db 74, 73, 65, 99, 194, 177, 196, 128, 61, 80, 64, 157, 189, 181, 178, 76
        db 76, 67, 104, 192, 174, 193, 121, 65, 81, 70, 165, 183, 183, 165, 71, 82
        db 66, 123, 190, 172, 187, 100, 73, 77, 87, 180, 174, 187, 137, 68, 85, 71
        db 154, 181, 177, 167, 77, 85, 71, 123, 186, 169, 182, 97, 79, 79, 97, 181
        db 168, 184, 121, 74, 86, 82, 168, 171, 179, 143, 74, 90, 76, 152, 176, 173
        db 159, 79, 90, 76, 135, 178, 168, 169, 87, 89, 79, 122, 178, 164, 173, 96
        db 87, 83, 112, 177, 162, 175, 104, 86, 86, 106, 174, 161, 174, 109, 86, 88
        db 103, 172, 160, 173, 112, 87, 90, 103, 170, 159, 172, 113, 88, 91, 104, 169
        db 158, 170, 111, 90, 92, 108, 169, 157, 168, 108, 93, 92, 113, 168, 156, 165
        db 103, 96, 91, 121, 167, 156, 160, 98, 99, 91, 131, 164, 157, 153, 94, 102
        db 92, 141, 160, 159, 142, 93, 103, 96, 151, 155, 161, 129, 95, 102, 105, 158
        db 152, 161, 115, 100, 99, 118, 160, 151, 155, 103, 105, 97, 135, 156, 154, 142
        db 99, 107, 101, 149, 150, 156, 124, 102, 105, 114, 155, 147, 153, 108, 108, 102
        db 133, 152, 150, 139, 103, 110, 106, 148, 146, 152, 120, 107, 106, 122, 151, 146
        db 145, 107, 112, 106, 141, 146, 149, 126, 108, 110, 119, 149, 143, 145, 111, 114
        db 108, 138, 144, 146, 128, 110, 113, 120, 146, 141, 142, 113, 116, 111, 138, 141
        db 144, 125, 113, 114, 124, 143, 140, 138, 114, 118, 115, 139, 138, 142, 121, 117
        db 115, 130, 140, 139, 131, 116, 119, 121, 139, 136, 137, 118, 120, 118, 135, 136
        db 138, 124, 120, 119, 129, 136, 136, 130, 120, 121, 124, 136, 134, 133, 121, 123
        db 122, 134, 132, 134, 124, 124, 123, 131, 132, 133, 127, 124, 124, 128, 131, 131
        db 128, 125, 126, 127, 130, 130, 129, 126, 127, 127, 129, 129, 129, 127, 128, 128
smpdie: db 128, 129, 129, 129, 128, 127, 125, 123, 119, 132, 139, 134, 134, 129, 128, 122
        db 119, 113, 110, 142, 149, 139, 139, 129, 127, 117, 114, 102, 104, 154, 157, 144
        db 142, 129, 126, 111, 109, 90, 98, 165, 165, 150, 146, 129, 126, 106, 104, 80
        db 91, 175, 174, 155, 150, 129, 125, 102, 99, 71, 80, 181, 185, 160, 156, 130
        db 126, 99, 93, 65, 64, 181, 200, 166, 164, 133, 127, 99, 88, 64, 43, 168
        db 218, 173, 172, 139, 128, 103, 81, 68, 24, 138, 235, 185, 179, 150, 128, 111
        db 76, 73, 17, 91, 236, 204, 179, 164, 129, 122, 81, 78, 34, 43, 205, 227
        db 179, 177, 136, 128, 96, 76, 59, 15, 142, 242, 189, 181, 153, 128, 115, 77
        db 76, 23, 66, 223, 217, 178, 173, 133, 127, 91, 77, 54, 17, 152, 242, 188
        db 181, 153, 128, 115, 77, 76, 25, 58, 216, 222, 178, 176, 136, 128, 98, 76
        db 64, 14, 118, 241, 199, 180, 163, 129, 123, 84, 78, 44, 26, 172, 239, 185
        db 180, 150, 128, 115, 77, 77, 27, 51, 208, 227, 179, 178, 141, 128, 106, 76
        db 72, 18, 78, 227, 216, 178, 175, 136, 128, 99, 76, 67, 15, 98, 235, 209
        db 178, 171, 133, 127, 95, 76, 64, 14, 109, 237, 205, 178, 170, 132, 127, 94
        db 76, 63, 14, 108, 237, 206, 178, 171, 133, 127, 96, 76, 66, 15, 96, 233
        db 212, 178, 174, 136, 128, 101, 76, 71, 18, 75, 222, 221, 178, 178, 141, 128
        db 109, 76, 76, 27, 49, 199, 233, 182, 180, 150, 128, 118, 80, 78, 42, 24
        db 160, 241, 192, 180, 163, 130, 125, 91, 77, 62, 14, 104, 234, 212, 178, 175
        db 138, 128, 107, 76, 76, 27, 46, 194, 235, 184, 180, 155, 128, 122, 85, 78
        db 55, 15, 120, 238, 207, 178, 174, 137, 128, 106, 76, 76, 28, 43, 188, 237
        db 186, 180, 159, 129, 125, 90, 77, 63, 15, 92, 227, 219, 178, 178, 145, 128
        db 116, 80, 78, 46, 20, 138, 240, 202, 178, 173, 136, 128, 107, 77, 77, 32
        db 34, 172, 240, 192, 179, 166, 132, 127, 100, 76, 74, 24, 49, 192, 237, 186
        db 180, 162, 130, 126, 96, 76, 71, 21, 57, 200, 235, 185, 180, 160, 130, 126
        db 95, 76, 71, 21, 57, 198, 235, 185, 180, 162, 130, 127, 97, 76, 73, 24
        db 47, 187, 238, 189, 179, 166, 132, 127, 103, 76, 76, 32, 33, 164, 241, 197
        db 178, 172, 137, 128, 111, 78, 78, 45, 19, 127, 237, 211, 178, 178, 146, 128
        db 120, 85, 78, 62, 16, 80, 215, 230, 182, 180, 160, 130, 127, 98, 76, 75
        db 30, 35, 165, 241, 199, 178, 174, 140, 128, 115, 81, 78, 56, 15, 93, 222
        db 226, 181, 180, 159, 130, 127, 99, 76, 76, 32, 30, 154, 240, 204, 178, 177
        db 144, 128, 120, 86, 77, 66, 18, 62, 197, 237, 189, 179, 169, 136, 128, 112
        db 80, 78, 54, 15, 93, 220, 228, 182, 180, 162, 131, 128, 105, 77, 78, 45
        db 18, 115, 230, 221, 180, 180, 158, 130, 127, 101, 77, 78, 41, 21, 125, 233
        db 218, 179, 180, 157, 130, 127, 101, 77, 78, 41, 20, 121, 232, 220, 180, 180
        db 159, 130, 127, 104, 77, 78, 47, 17, 105, 224, 226, 182, 180, 164, 133, 128
        db 110, 79, 79, 57, 16, 78, 206, 235, 189, 178, 172, 139, 128, 118, 86, 77
        db 69, 22, 45, 170, 240, 203, 177, 178, 150, 129, 125, 97, 76, 77, 40, 20
        db 116, 228, 224, 182, 179, 165, 134, 128, 113, 82, 78, 64, 19, 55, 181, 240
        db 200, 177, 177, 149, 129, 126, 98, 77, 78, 44, 18, 102, 220, 230, 185, 179
        db 170, 138, 128, 119, 88, 77, 73, 29, 31, 142, 235, 217, 179, 179, 162, 133
        db 128, 113, 82, 78, 66, 21, 46, 167, 239, 208, 178, 179, 157, 131, 128, 109
        db 80, 79, 62, 19, 55, 178, 240, 204, 177, 179, 155, 130, 127, 108, 79, 79
        db 61, 19, 54, 175, 240, 206, 177, 179, 157, 131, 128, 110, 81, 78, 65, 22
        db 44, 160, 238, 213, 179, 179, 163, 133, 128, 116, 85, 77, 72, 30, 28, 131
        db 230, 224, 183, 179, 170, 140, 128, 122, 93, 77, 78, 45, 17, 88, 206, 237
        db 195, 177, 177, 151, 129, 127, 107, 80, 79, 64, 21, 43, 156, 237, 216, 180
        db 179, 167, 137, 128, 121, 92, 77, 77, 44, 17, 86, 202, 238, 197, 177, 178
        db 155, 130, 128, 112, 83, 78, 71, 29, 28, 125, 225, 229, 187, 178, 175, 146
        db 129, 126, 104, 79, 79, 64, 22, 40, 149, 234, 221, 182, 178, 171, 142, 128
        db 125, 100, 78, 79, 60, 20, 47, 158, 236, 219, 181, 179, 171, 141, 128, 125
        db 100, 78, 79, 61, 20, 44, 152, 234, 221, 183, 178, 172, 144, 128, 126, 104
        db 79, 79, 66, 25, 33, 132, 227, 229, 188, 177, 176, 150, 130, 128, 111, 83
        db 78, 74, 36, 21, 98, 207, 237, 199, 177, 179, 161, 134, 128, 120, 92, 77
        db 79, 54, 18, 55, 165, 236, 218, 182, 178, 173, 145, 129, 127, 108, 81, 78
        db 72, 33, 22, 101, 208, 237, 200, 177, 179, 163, 135, 128, 122, 96, 78, 79
        db 61, 22, 38, 138, 227, 229, 189, 177, 177, 155, 131, 128, 118, 90, 77, 79
        db 53, 18, 52, 158, 234, 223, 185, 177, 176, 151, 130, 128, 115, 88, 77, 78
        db 50, 18, 57, 163, 235, 222, 184, 177, 176, 151, 130, 128, 116, 89, 77, 79
        db 53, 19, 50, 152, 231, 226, 187, 177, 177, 156, 132, 128, 120, 94, 77, 79
        db 61, 23, 35, 126, 219, 234, 196, 177, 179, 164, 137, 128, 125, 103, 80, 79
        db 72, 35, 20, 85, 190, 238, 212, 180, 178, 173, 148, 129, 128, 115, 89, 77
        db 79, 56, 20, 41, 136, 224, 232, 195, 177, 179, 164, 138, 128, 126, 105, 81
        db 78, 75, 42, 18, 67, 170, 235, 222, 186, 177, 177, 157, 133, 128, 123, 99
        db 79, 79, 71, 34, 21, 84, 186, 238, 216, 182, 177, 176, 154, 132, 128, 121
        db 97, 78, 79, 69, 33, 21, 86, 187, 238, 216, 183, 177, 176, 155, 132, 128
        db 122, 99, 79, 79, 72, 37, 19, 73, 173, 235, 223, 187, 177, 178, 160, 136
        db 128, 125, 106, 82, 78, 77, 48, 19, 49, 143, 224, 233, 197, 177, 179, 169
        db 144, 129, 128, 115, 90, 78, 80, 64, 28, 25, 96, 193, 238, 216, 183, 177
        db 177, 157, 134, 128, 125, 105, 82, 78, 77, 50, 19, 44, 133, 218, 235, 203
        db 178, 178, 173, 149, 131, 128, 121, 98, 79, 79, 74, 42, 18, 58, 152, 227
        db 231, 197, 177, 178, 170, 146, 130, 128, 119, 96, 79, 79, 72, 40, 19, 61
        db 154, 227, 231, 197, 177, 178, 171, 147, 130, 128, 121, 98, 79, 79, 75, 45
        db 19, 50, 139, 219, 235, 203, 178, 178, 174, 153, 132, 128, 124, 105, 83, 78
        db 78, 56, 23, 32, 107, 198, 237, 216, 184, 176, 178, 163, 139, 128, 127, 115
        db 91, 78, 80, 70, 37, 19, 62, 153, 225, 233, 200, 178, 178, 174, 153, 133
        db 128, 125, 106, 84, 78, 79, 61, 27, 25, 87, 179, 234, 226, 192, 176, 178
        db 170, 148, 130, 128, 123, 102, 82, 78, 78, 57, 24, 29, 96, 187, 235, 223
        db 190, 176, 178, 170, 147, 130, 128, 123, 103, 82, 78, 79, 59, 26, 26, 88
        db 179, 233, 227, 194, 176, 178, 172, 151, 132, 128, 125, 108, 86, 78, 80, 66
        db 33, 20, 65, 153, 223, 234, 204, 179, 177, 176, 160, 138, 128, 127, 117, 94
        db 79, 80, 76, 49, 21, 36, 108, 194, 236, 222, 190, 176, 178, 171, 150, 132
        db 128, 125, 109, 87, 78, 80, 69, 38, 19, 53, 134, 212, 237, 213, 184, 176
        db 178, 167, 145, 130, 128, 123, 105, 84, 78, 80, 66, 35, 20, 59, 142, 215
        db 236, 211, 183, 176, 178, 167, 145, 130, 128, 124, 106, 85, 78, 80, 69, 38
        db 19, 51, 130, 207, 237, 217, 187, 176, 178, 171, 150, 132, 128, 126, 112, 90
        db 78, 80, 75, 49, 22, 34, 99, 183, 232, 228, 197, 177, 177, 176, 160, 139
        db 129, 128, 121, 101, 82, 78, 80, 65, 34, 20, 56, 134, 209, 236, 217, 187
        db 176, 178, 172, 153, 134, 128, 127, 116, 95, 80, 79, 79, 59, 29, 23, 69
        db 150, 217, 236, 212, 184, 176, 178, 171, 151, 133, 128, 127, 115, 94, 80, 79
        db 79, 59, 29, 22, 66, 145, 214, 236, 215, 186, 176, 178, 173, 154, 135, 128
        db 128, 118, 98, 82, 79, 80, 66, 37, 20, 48, 120, 197, 235, 225, 195, 177
        db 177, 176, 162, 142, 129, 128, 124, 108, 88, 78, 80, 76, 52, 25, 27, 78
        db 157, 220, 235, 213, 185, 175, 178, 173, 155, 136, 128, 128, 120, 102, 84, 78
        db 81, 72, 45, 21, 34, 93, 172, 226, 233, 208, 182, 175, 178, 171, 153, 135
        db 128, 128, 120, 101, 83, 78, 81, 72, 46, 22, 32, 89, 167, 223, 234, 211
        db 185, 175, 177, 173, 156, 137, 129, 128, 122, 106, 87, 79, 80, 76, 54, 27
        db 24, 67, 141, 208, 235, 221, 193, 177, 176, 176, 164, 145, 131, 128, 126, 115
        db 96, 81, 79, 80, 68, 41, 21, 36, 95, 170, 224, 234, 211, 185, 175, 177
        db 174, 159, 140, 129, 128, 125, 111, 91, 80, 80, 80, 65, 37, 21, 42, 104
        db 178, 227, 233, 209, 184, 175, 177, 174, 159, 140, 129, 128, 125, 112, 93, 80
        db 81, 49, 24, 124, 229, 211, 176, 177, 149, 128, 124, 93, 79, 73, 28, 53
        db 181, 234, 190, 176, 170, 137, 128, 115, 83, 81, 59, 21, 92, 214, 224, 180
        db 177, 160, 131, 127, 105, 79, 80, 46, 26, 126, 229, 212, 176, 177, 152, 129
        db 126, 98, 79, 78, 37, 34, 148, 234, 204, 175, 176, 148, 128, 124, 94, 79
        db 76, 33, 40, 158, 235, 201, 175, 175, 147, 128, 124, 94, 79, 76, 33, 39
        db 155, 234, 203, 175, 176, 149, 128, 125, 96, 79, 78, 38, 32, 140, 232, 209
        db 176, 177, 154, 129, 127, 102, 79, 80, 47, 24, 113, 222, 220, 179, 177, 162
        db 132, 128, 111, 82, 81, 61, 22, 76, 198, 231, 188, 176, 171, 140, 128, 121
        db 90, 79, 75, 32, 40, 153, 233, 206, 175, 177, 154, 130, 127, 105, 80, 81
        db 54, 21, 90, 207, 229, 185, 176, 170, 139, 128, 121, 91, 79, 76, 35, 35
        db 141, 230, 212, 177, 177, 159, 132, 128, 112, 83, 81, 66, 24, 59, 178, 234
        db 198, 175, 176, 151, 129, 127, 104, 80, 81, 56, 22, 81, 198, 232, 190, 175
        db 174, 146, 128, 125, 99, 79, 81, 51, 22, 92, 206, 230, 188, 175, 172, 144
        db 128, 125, 98, 79, 81, 50, 22, 92, 205, 230, 188, 175, 173, 145, 128, 125
        db 101, 80, 81, 55, 22, 79, 195, 233, 194, 175, 175, 150, 129, 127, 106, 81
        db 81, 64, 24, 58, 171, 234, 204, 175, 177, 159, 132, 128, 115, 86, 80, 74
        db 34, 34, 132, 225, 220, 181, 176, 169, 140, 128, 124, 97, 79, 81, 53, 22
        db 80, 193, 233, 196, 175, 176, 154, 131, 128, 113, 85, 80, 73, 33, 35, 131
        db 224, 221, 182, 176, 171, 143, 128, 125, 101, 80, 81, 61, 24, 58, 168, 233
        db 208, 176, 177, 164, 136, 128, 121, 94, 79, 80, 51, 22, 78, 189, 234, 200
        db 175, 177, 159, 133, 128, 119, 91, 79, 79, 47, 23, 88, 196, 233, 197, 175
        db 176, 158, 133, 128, 118, 90, 80, 79, 48, 23, 84, 193, 233, 199, 175, 177
        db 160, 134, 128, 120, 93, 79, 81, 54, 22, 69, 177, 233, 207, 176, 176, 166
        db 138, 128, 124, 100, 80, 82, 64, 26, 47, 147, 227, 219, 182, 175, 172, 147
        db 129, 127, 110, 85, 81, 75, 39, 27, 101, 203, 232, 196, 175, 176, 160, 134
        db 128, 122, 96, 80, 82, 61, 25, 51, 152, 228, 219, 182, 175, 173, 148, 129
        db 128, 113, 87, 80, 78, 46, 23, 80, 184, 233, 206, 177, 176, 168, 141, 128
        db 126, 107, 83, 81, 74, 38, 27, 99, 200, 232, 199, 175, 176, 164, 138, 128
        db 125, 104, 82, 81, 72, 35, 29, 105, 204, 232, 198, 175, 176, 164, 138, 128
        db 125, 104, 82, 81, 74, 38, 27, 97, 197, 233, 202, 176, 176, 167, 141, 128
        db 127, 109, 85, 81, 78, 46, 23, 76, 177, 232, 212, 179, 175, 172, 149, 130
        db 128, 117, 91, 80, 81, 59, 25, 47, 140, 221, 226, 189, 174, 176, 160, 135
        db 128, 124, 103, 82, 81, 74, 40, 26, 87, 186, 233, 209, 178, 175, 172, 149
        db 130, 128, 118, 93, 80, 82, 64, 28, 38, 121, 210, 231, 197, 175, 176, 167
        db 142, 128, 127, 112, 88, 80, 81, 56, 25, 49, 140, 220, 227, 192, 174, 176
        db 164, 139, 128, 127, 110, 86, 80, 80, 54, 24, 52, 142, 220, 227, 192, 174
        db 176, 165, 140, 128, 127, 112, 88, 80, 81, 58, 26, 44, 129, 213, 230, 197
        db 175, 176, 168, 144, 129, 128, 117, 92, 80, 82, 67, 32, 31, 101, 194, 232
        db 209, 179, 175, 173, 153, 132, 128, 123, 102, 82, 81, 77, 47, 24, 62, 154
        db 224, 225, 190, 174, 176, 165, 141, 129, 128, 115, 91, 80, 82, 67, 33, 30
        db 95, 187, 232, 213, 181, 174, 175, 158, 135, 128, 126, 109, 86, 80, 81, 60
        db 27, 39, 115, 202, 232, 206, 178, 175, 174, 155, 133, 128, 125, 106, 85, 81
        db 81, 57, 26, 41, 118, 203, 232, 206, 178, 175, 174, 155, 134, 128, 126, 108
        db 86, 80, 81, 61, 29, 35, 105, 193, 232, 212, 181, 174, 175, 160, 138, 128
        db 127, 114, 91, 80, 82, 70, 37, 27, 78, 167, 227, 223, 190, 174, 176, 168
        db 146, 130, 128, 122, 100, 82, 81, 79, 53, 25, 45, 122, 204, 232, 208, 179
        db 174, 175, 159, 137, 128, 127, 115, 92, 80, 82, 73, 42, 25, 64, 150, 219
        db 228, 198, 176, 175, 173, 154, 134, 128, 126, 110, 89, 80, 82, 69, 37, 26
        db 74, 160, 223, 226, 195, 175, 175, 172, 153, 133, 128, 126, 110, 89, 80, 82
        db 70, 39, 26, 68, 153, 219, 228, 199, 176, 175, 173, 156, 136, 128, 127, 115
        db 93, 81, 82, 75, 47, 25, 51, 128, 205, 231, 209, 181, 174, 175, 164, 142
        db 129, 128, 121, 101, 83, 81, 81, 61, 31, 31, 88, 172, 226, 224, 193, 175
        db 175, 172, 154, 135, 128, 127, 115, 93, 81, 82, 77, 50, 26, 44, 114, 194
        db 231, 216, 186, 174, 175, 169, 149, 132, 128, 126, 111, 90, 81, 83, 74, 46
        db 25, 49, 122, 199, 231, 214, 185, 173, 175, 169, 149, 132, 128, 126, 111, 91
        db 81, 83, 76, 49, 26, 44, 112, 191, 230, 218, 189, 174, 175, 171, 153, 134
        db 128, 127, 116, 96, 82, 82, 80, 59, 30, 32, 86, 166, 222, 227, 199, 177
        db 174, 175, 162, 141, 129, 128, 123, 106, 87, 81, 83, 72, 44, 25, 50, 120
        db 195, 230, 217, 188, 174, 175, 172, 155, 136, 128, 128, 119, 100, 84, 81, 82
        db 67, 37, 26, 62, 137, 206, 231, 213, 185, 173, 175, 170, 153, 135, 128, 127
        db 118, 99, 83, 81, 82, 66, 38, 26, 60, 134, 203, 231, 215, 186, 173, 175
        db 172, 155, 136, 128, 128, 121, 102, 85, 81, 83, 72, 45, 25, 47, 112, 186
        db 228, 223, 195, 175, 174, 174, 162, 143, 130, 128, 125, 111, 92, 81, 82, 80
        db 59, 32, 30, 74, 148, 211, 230, 211, 184, 173, 175, 171, 155, 137, 128, 128
        db 122, 105, 87, 81, 83, 76, 51, 28, 36, 89, 164, 219, 229, 206, 181, 173
        db 175, 170, 153, 135, 128, 128, 121, 104, 86, 81, 83, 76, 52, 28, 35, 87
        db 161, 217, 229, 208, 183, 173, 175, 171, 156, 137, 129, 128, 123, 108, 89, 81
        db 83, 79, 59, 33, 29, 68, 138, 203, 230, 217, 190, 174, 174, 174, 163, 144
        db 131, 128, 127, 116, 98, 84, 81, 83, 72, 46, 27, 40, 95, 167, 219, 229
        db 207, 182, 173, 175, 172, 157, 139, 129, 128, 125, 112, 93, 82, 82, 82, 68
        db 42, 26, 46, 105, 176, 222, 227, 205, 181, 173, 175, 171, 157, 139, 129, 128
        db 125, 112, 94, 82, 82, 83, 70, 44, 26, 41, 95, 166, 217, 229, 210, 185
        db 173, 174, 173, 161, 143, 131, 128, 127, 117, 100, 85, 81, 83, 77, 55, 31
        db 30, 68, 135, 198, 228, 221, 196, 177, 173, 175, 169, 153, 137, 129, 128, 124
        db 111, 94, 83, 82, 83, 71, 47, 27, 37, 84, 152, 209, 230, 217, 191, 175
        db 173, 175, 167, 151, 135, 128, 128, 124, 110, 93, 82, 82, 83, 71, 47, 28
        db 36, 81, 148, 206, 229, 219, 194, 176, 173, 175, 169, 154, 137, 129, 128, 126
        db 114, 97, 84, 82, 83, 77, 56, 32, 29, 61, 123, 187, 224, 226, 204, 182
        db 173, 174, 173, 162, 145, 132, 128, 128, 122, 107, 91, 82, 83, 83, 70, 47
        db 28, 36, 78, 143, 201, 228, 222, 198, 178, 173, 174, 172, 159, 142, 131, 128
        db 127, 120, 105, 89, 82, 83, 82, 70, 47, 28, 35, 77, 141, 198, 227, 223
        db 200, 180, 173, 174, 173, 162, 145, 132, 128, 128, 123, 109, 93, 83, 82, 83
        db 75, 55, 32, 29, 59, 116, 178, 220, 228, 211, 187, 174, 173, 174, 168, 154
        db 138, 129, 128, 127, 118, 103, 88, 82, 83, 82, 70, 47, 29, 34, 73, 133
        db 191, 225, 226, 206, 184, 173, 173, 174, 167, 152, 137, 129, 128, 126, 117, 102
        db 88, 82, 83, 82, 70, 48, 29, 33, 68, 127, 186, 222, 227, 209, 187, 174
        db 173, 174, 169, 156, 140, 130, 128, 127, 121, 107, 92, 83, 82, 84, 77, 58
        db 36, 28, 49, 98, 159, 208, 228, 220, 198, 179, 172, 174, 173, 165, 149, 135
        db 129, 128, 126, 117, 103, 88, 82, 83, 83, 73, 53, 32, 29, 55, 107, 167
        db 212, 228, 218, 196, 178, 172, 174, 173, 164, 149, 136, 129, 128, 127, 119, 104
        db 90, 82, 83, 84, 76, 57, 36, 28, 47, 93, 152, 202, 227, 223, 203, 183
        db 173, 173, 174, 169, 156, 141, 131, 128, 128, 123, 112, 97, 85, 82, 84, 82
        db 69, 48, 30, 31, 61, 113, 170, 213, 228, 219, 197, 179, 172, 173, 174, 167
        db 153, 139, 130, 128, 128, 123, 111, 96, 85, 82, 84, 82, 70, 49, 31, 31
        db 58, 108, 165, 209, 228, 221, 201, 182, 173, 173, 174, 169, 157, 142, 132, 128
        db 128, 125, 115, 101, 88, 82, 83, 84, 76, 59, 38, 28, 41, 81, 136, 188
        db 221, 227, 213, 192, 177, 172, 173, 173, 166, 152, 138, 130, 128, 128, 123, 112
        db 98, 86, 82, 83, 83, 75, 56, 36, 28, 43, 83, 138, 189, 221, 227, 213
        db 193, 177, 172, 173, 173, 167, 154, 140, 131, 128, 128, 125, 115, 102, 89, 83
        db 83, 84, 79, 64, 43, 29, 34, 64, 114, 168, 209, 227, 222, 203, 184, 174
        db 174, 166, 141, 128, 127, 109, 86, 83, 79, 48, 30, 85, 182, 228, 204, 175
        db 173, 168, 144, 129, 127, 113, 89, 83, 82, 55, 28, 68, 163, 225, 212, 179
        db 172, 171, 151, 131, 128, 120, 96, 82, 84, 67, 33, 45, 128, 211, 223, 189
        db 172, 174, 161, 137, 128, 126, 107, 85, 83, 79, 48, 30, 81, 175, 226, 208
        db 177, 172, 171, 150, 131, 128, 120, 97, 83, 84, 70, 36, 39, 113, 201, 226
        db 196, 173, 173, 166, 143, 129, 128, 115, 91, 82, 84, 63, 31, 49, 133, 212
        db 223, 190, 172, 174, 163, 140, 128, 127, 112, 89, 83, 83, 60, 30, 52, 137
        db 213, 223, 190, 172, 174, 164, 140, 128, 127, 113, 90, 83, 84, 63, 32, 47
        db 126, 207, 225, 194, 173, 173, 167, 144, 129, 128, 118, 95, 83, 84, 71, 38
        db 36, 101, 189, 227, 205, 176, 172, 171, 152, 132, 128, 124, 104, 85, 84, 80
        db 51, 29, 66, 153, 219, 220, 187, 172, 173, 163, 141, 129, 128, 116, 93, 83
        db 84, 70, 38, 36, 98, 185, 226, 208, 178, 172, 172, 156, 135, 128, 126, 109
        db 88, 83, 83, 62, 32, 45, 118, 200, 226, 201, 175, 172, 171, 152, 133, 128
        db 125, 107, 87, 83, 83, 60, 31, 48, 123, 202, 226, 200, 175, 172, 171, 153
        db 133, 128, 125, 108, 87, 83, 83, 63, 33, 43, 112, 194, 227, 205, 177, 172
        db 172, 157, 136, 128, 127, 113, 91, 83, 85, 71, 39, 34, 87, 172, 223, 215
        db 184, 171, 173, 164, 143, 129, 128, 120, 100, 84, 84, 80, 54, 30, 55, 131
        db 205, 226, 200, 175, 172, 172, 155, 135, 128, 127, 113, 92, 83, 85, 73, 43
        db 31, 76, 159, 218, 221, 191, 172, 173, 169, 149, 132, 128, 125, 109, 89, 83
        db 84, 69, 38, 34, 87, 169, 222, 218, 188, 172, 173, 168, 148, 131, 128, 125
        db 108, 88, 83, 84, 69, 39, 33, 83, 164, 220, 220, 190, 172, 173, 169, 151
        db 133, 128, 126, 112, 91, 83, 85, 75, 46, 30, 66, 143, 210, 225, 199, 175
        db 172, 172, 158, 138, 128, 128, 119, 99, 84, 84, 82, 59, 32, 43, 105, 184
        db 224, 214, 185, 171, 173, 167, 148, 132, 128, 126, 111, 91, 83, 85, 76, 48
        db 30, 59, 133, 203, 226, 205, 178, 171, 173, 163, 143, 130, 128, 124, 107, 88
        db 83, 85, 72, 44, 31, 67, 142, 208, 225, 202, 177, 171, 173, 163, 143, 130
        db 128, 124, 107, 89, 83, 85, 73, 45, 31, 62, 134, 202, 226, 206, 179, 171
        db 173, 165, 146, 131, 128, 126, 112, 92, 83, 85, 79, 54, 31, 47, 109, 184
        db 224, 216, 188, 172, 172, 170, 155, 136, 128, 128, 120, 101, 86, 84, 84, 68
        db 40, 32, 71, 145, 207, 225, 204, 179, 171, 173, 166, 147, 132, 128, 126, 114
        db 95, 84, 85, 82, 61, 35, 37, 87, 161, 215, 223, 198, 176, 171, 173, 163
        db 145, 131, 128, 126, 113, 94, 84, 85, 81, 60, 35, 38, 86, 160, 214, 223
        db 200, 177, 171, 173, 165, 147, 132, 128, 127, 116, 97, 84, 84, 83, 66, 39
        db 33, 71, 141, 203, 225, 208, 182, 171, 172, 169, 153, 136, 128, 128, 121, 104
        db 88, 83, 85, 76, 52, 32, 47, 104, 175, 220, 220, 195, 175, 171, 172, 164
        db 146, 132, 128, 127, 116, 98, 85, 84, 84, 70, 44, 31, 58, 122, 189, 223
        db 216, 190, 173, 171, 172, 161, 143, 131, 128, 126, 115, 97, 85, 84, 84, 70
        db 44, 31, 58, 121, 187, 223, 217, 191, 173, 171, 172, 163, 145, 132, 128, 127
        db 118, 100, 86, 84, 85, 75, 50, 32, 47, 101, 170, 217, 222, 200, 178, 171
        db 172, 168, 153, 136, 128, 128, 123, 108, 91, 84, 85, 82, 64, 39, 33, 68
        db 132, 195, 224, 215, 190, 173, 171, 172, 164, 146, 132, 128, 127, 120, 103, 88
        db 84, 86, 79, 58, 35, 37, 77, 144, 202, 224, 212, 187, 172, 171, 172, 163
        db 145, 132, 128, 127, 120, 103, 88, 84, 86, 80, 60, 37, 35, 71, 135, 195
        db 223, 215, 191, 173, 171, 172, 166, 150, 134, 128, 128, 123, 109, 92, 84, 85
        db 84, 69, 44, 32, 53, 108, 173, 216, 222, 202, 180, 171, 172, 170, 159, 142
        db 130, 128, 127, 118, 102, 88, 84, 86, 80, 61, 38, 34, 66, 127, 188, 221
        db 219, 196, 176, 170, 172, 169, 156, 139, 130, 128, 126, 117, 100, 87, 84, 86
        db 80, 60, 38, 34, 66, 125, 186, 220, 220, 198, 178, 170, 172, 170, 158, 142
        db 131, 128, 127, 119, 104, 89, 84, 86, 83, 67, 43, 32, 52, 104, 166, 212
        db 224, 208, 184, 172, 171, 172, 164, 149, 135, 128, 128, 125, 113, 97, 86, 84
        db 86, 78, 58, 37, 35, 67, 125, 184, 219, 221, 201, 179, 170, 171, 171, 161
        db 146, 133, 128, 128, 123, 111, 95, 85, 85, 86, 77, 57, 36, 36, 68, 125
        db 183, 218, 221, 202, 181, 171, 171, 171, 163, 148, 134, 128, 128, 125, 114, 98
        db 87, 84, 86, 81, 63, 41, 33, 54, 104, 164, 209, 224, 211, 188, 173, 170
        db 172, 168, 155, 140, 130, 128, 127, 121, 107, 92, 85, 85, 85, 76, 55, 36
        db 36, 67, 122, 180, 216, 222, 205, 183, 171, 170, 172, 166, 153, 138, 130, 128
        db 127, 120, 106, 91, 84, 85, 85, 76, 55, 36, 36, 65, 119, 176, 214, 223
        db 208, 186, 172, 170, 172, 168, 156, 141, 131, 128, 128, 123, 110, 95, 86, 85
        db 86, 81, 64, 42, 33, 50, 94, 152, 200, 222, 217, 196, 178, 170, 171, 171
        db 164, 149, 136, 129, 128, 127, 119, 105, 91, 85, 85, 86, 77, 58, 39, 34
        db 57, 105, 162, 206, 223, 214, 194, 176, 170, 171, 171, 163, 149, 136, 129, 128
        db 127, 119, 106, 92, 85, 85, 86, 79, 62, 41, 33, 50, 94, 150, 197, 221
        db 218, 200, 180, 171, 170, 172, 167, 154, 140, 131, 128, 128, 124, 113, 98, 87
        db 84, 86, 84, 72, 52, 36, 37, 65, 115, 169, 209, 223, 213, 193, 176, 170
        db 171, 171, 164, 151, 138, 130, 128, 128, 122, 111, 97, 87, 85, 86, 84, 72
        db 52, 36, 37, 64, 113, 166, 207, 223, 215, 195, 178, 170, 170, 171, 166, 154
        db 140, 131, 128, 128, 125, 115, 101, 89, 85, 86, 86, 78, 60, 41, 34, 49
        db 89, 141, 190, 218, 221, 206, 186, 173, 170, 171, 170, 162, 149, 136, 129, 128
        db 127, 123, 111, 98, 88, 85, 86, 85, 76, 57, 39, 34, 52, 93, 145, 192
        db 218, 221, 206, 186, 173, 169, 171, 170, 163, 150, 138, 130, 128, 128, 124, 114
        db 100, 89, 85, 86, 86, 79, 63, 44, 34, 43, 76, 125, 175, 210, 222, 214
        db 195, 178, 170, 170, 171, 168, 158, 144, 134, 129, 128, 127, 121, 110, 96, 87
        db 85, 86, 85, 76, 59, 41, 34, 47, 83, 133, 180, 213, 222, 212, 194, 177
        db 170, 170, 171, 168, 158, 145, 134, 129, 128, 127, 122, 112, 99, 89, 85, 86
        db 86, 80, 64, 46, 34, 41, 69, 115, 164, 203, 221, 218, 202, 184, 172, 169
        db 171, 170, 164, 152, 140, 131, 128, 128, 126, 119, 107, 95, 87, 85, 86, 85
        db 77, 60, 42, 34, 45, 76, 122, 170, 206, 221, 217, 200, 183, 172, 169, 171
        db 170, 164, 153, 140, 132, 128, 128, 127, 120, 109, 97, 88, 85, 86, 86, 80
        db 66, 47, 35, 39, 63, 104, 152, 193, 217, 221, 209, 191, 176, 170, 170, 171
        db 168, 160, 148, 136, 130, 128, 128, 125, 117, 106, 94, 87, 85, 87, 86, 78
        db 63, 45, 35, 41, 66, 108, 155, 195, 218, 220, 208, 191, 176, 170, 170, 171
        db 169, 161, 149, 138, 131, 128, 128, 126, 120, 109, 97, 88, 85, 86, 87, 82
        db 69, 52, 38, 36, 52, 87, 132, 176, 208, 221, 216, 201, 184, 173, 169, 170
        db 171, 167, 158, 146, 135, 130, 128, 128, 125, 118, 107, 96, 88, 85, 86, 87
        db 81, 69, 52, 38, 36, 52, 85, 129, 173, 205, 220, 218, 204, 187, 174, 169
        db 170, 171, 168, 161, 149, 138, 131, 128, 128, 127, 122, 112, 101, 91, 86, 86
        db 87, 85, 77, 62, 45, 35, 40, 62, 99, 144, 184, 212, 221, 215, 199, 183
        db 173, 169, 170, 171, 168, 160, 148, 138, 131, 128, 128, 127, 122, 113, 101, 91
        db 86, 85, 87, 86, 79, 65, 49, 37, 37, 55, 88, 130, 172, 204, 220, 218
        db 206, 190, 176, 170, 169, 171, 170, 164, 155, 143, 134, 129, 128, 128, 126, 119
        db 109, 98, 89, 86, 86, 87, 85, 77, 63, 47, 36, 38, 57, 91, 133, 173
        db 204, 219, 218, 206, 190, 177, 170, 169, 170, 170, 166, 157, 146, 136, 130, 128
        db 128, 127, 122, 113, 102, 92, 87, 85, 87, 87, 82, 70, 55, 41, 35, 44
        db 69, 106, 148, 186, 211, 220, 216, 202, 186, 175, 169, 169, 170, 170, 165, 155
        db 145, 135, 130, 128, 128, 127, 122, 113, 103, 93, 87, 86, 87, 87, 83, 73
        db 58, 44, 36, 41, 61, 94, 134, 174, 203, 218, 219, 208, 193, 179, 171, 169
        db 170, 170, 168, 161, 151, 141, 133, 129, 128, 128, 126, 120, 111, 100, 92, 87
        db 86, 87, 87, 83, 72, 57, 43, 36, 41, 60, 93, 132, 171, 201, 217, 219
        db 210, 196, 182, 172, 169, 169, 170, 169, 164, 155, 144, 135, 130, 128, 128, 127
        db 123, 116, 106, 96, 89, 86, 86, 87, 86, 80, 68, 53, 40, 36, 44, 66
        db 134, 197, 220, 203, 178, 168, 170, 165, 148, 132, 128, 127, 116, 98, 87, 87
        db 85, 66, 41, 41, 86, 156, 209, 218, 195, 173, 169, 170, 162, 144, 131, 128
        db 126, 113, 95, 86, 87, 84, 63, 39, 44, 92, 162, 211, 217, 194, 173, 169
        db 170, 162, 144, 131, 128, 126, 114, 96, 86, 87, 85, 66, 41, 41, 83, 151
        db 206, 219, 199, 176, 168, 170, 165, 148, 133, 128, 127, 119, 102, 88, 86, 87
        db 75, 49, 37, 62, 123, 188, 219, 210, 185, 170, 169, 169, 157, 140, 129, 128
        db 125, 112, 95, 86, 87, 84, 65, 41, 41, 81, 147, 203, 219, 202, 178, 168
        db 170, 167, 153, 136, 128, 128, 123, 108, 92, 86, 88, 83, 62, 40, 43, 87
        db 153, 205, 219, 201, 177, 168, 170, 167, 153, 136, 129, 128, 124, 109, 93, 86
        db 88, 84, 65, 42, 40, 78, 142, 198, 219, 206, 182, 169, 169, 169, 157, 140
        db 130, 128, 126, 115, 98, 87, 87, 87, 74, 50, 37, 57, 113, 176, 215, 215
        db 193, 173, 168, 170, 165, 150, 134, 128, 128, 123, 108, 93, 86, 88, 84, 67
        db 44, 39, 71, 131, 190, 218, 211, 187, 171, 168, 170, 162, 147, 133, 128, 128
        db 121, 107, 92, 86, 88, 84, 66, 43, 39, 72, 131, 189, 218, 211, 188, 171
        db 168, 170, 164, 148, 134, 128, 128, 123, 110, 94, 86, 88, 86, 71, 48, 37
        db 59, 113, 174, 213, 216, 196, 175, 168, 170, 167, 155, 139, 130, 128, 126, 117
        db 101, 89, 86, 88, 81, 61, 40, 42, 79, 139, 193, 218, 210, 188, 171, 168
        db 170, 164, 150, 136, 129, 128, 125, 113, 98, 88, 87, 88, 78, 57, 39, 45
        db 85, 145, 197, 218, 209, 186, 171, 168, 170, 164, 150, 136, 129, 128, 125, 115
        db 99, 88, 87, 88, 81, 61, 41, 42, 75, 132, 188, 216, 213, 192, 174, 168
        db 169, 167, 155, 140, 130, 128, 127, 120, 105, 92, 86, 88, 86, 71, 49, 38
        db 54, 101, 160, 205, 218, 205, 183, 170, 168, 169, 164, 150, 136, 129, 128, 126
        db 116, 101, 89, 87, 88, 84, 67, 46, 38, 60, 109, 167, 208, 218, 203, 182
        db 169, 168, 169, 164, 150, 136, 129, 128, 126, 117, 102, 90, 87, 88, 85, 70
        db 49, 38, 54, 98, 156, 201, 218, 208, 187, 171, 168, 169, 166, 155, 140, 130
        db 128, 127, 122, 109, 95, 87, 87, 88, 79, 59, 41, 42, 72, 124, 178, 212
        db 216, 200, 179, 169, 168, 169, 163, 150, 136, 129, 128, 127, 119, 105, 92, 87
        db 88, 87, 76, 56, 40, 44, 77, 130, 182, 213, 216, 199, 179, 169, 168, 169
        db 164, 151, 137, 129, 128, 127, 120, 107, 94, 87, 88, 88, 80, 61, 42, 40
        db 66, 114, 168, 207, 218, 205, 185, 171, 168, 169, 167, 157, 143, 132, 128, 128
        db 125, 114, 100, 90, 87, 88, 86, 73, 53, 39, 46, 81, 133, 183, 213, 216
        db 200, 180, 169, 168, 169, 165, 154, 140, 131, 128, 128, 124, 113, 99, 89, 87
        db 89, 86, 72, 53, 39, 46, 80, 131, 181, 212, 216, 202, 182, 170, 167, 169
        db 166, 156, 143, 132, 128, 128, 125, 116, 103, 91, 87, 88, 88, 79, 60, 43
        db 40, 62, 107, 159, 200, 217, 210, 192, 175, 168, 168, 169, 163, 151, 138, 130
        db 128, 128, 123, 113, 99, 89, 87, 89, 87, 75, 56, 41, 42, 68, 114, 165
        db 203, 217, 209, 190, 174, 168, 168, 169, 163, 151, 139, 130, 128, 128, 124, 114
        db 101, 91, 87, 88, 88, 79, 61, 44, 40, 59, 100, 151, 193, 215, 214, 197
        db 179, 169, 167, 169, 166, 157, 144, 133, 129, 128, 127, 120, 108, 96, 88, 87
        db 89, 86, 73, 55, 41, 43, 69, 114, 163, 201, 217, 211, 193, 177, 168, 168
        db 169, 166, 156, 143, 133, 128, 128, 127, 120, 108, 96, 88, 87, 89, 86, 75
        db 57, 42, 41, 63, 105, 154, 194, 215, 214, 198, 181, 169, 167, 169, 167, 160
        db 148, 136, 130, 128, 128, 124, 114, 102, 92, 87, 88, 89, 83, 68, 50, 39
        db 47, 77, 122, 169, 203, 217, 210, 193, 177, 168, 167, 169, 167, 158, 146, 135
        db 129, 128, 128, 123, 114, 101, 91, 87, 88, 89, 83, 69, 51, 40, 45, 72
        db 115, 162, 199, 216, 213, 197, 180, 169, 167, 168, 168, 161, 150, 138, 131, 128
        db 128, 126, 118, 107, 95, 89, 87, 89, 87, 78, 61, 45, 40, 53, 87, 132
        db 176, 206, 216, 209, 192, 176, 168, 167, 169, 167, 159, 148, 137, 130, 128, 128
        db 125, 118, 106, 95, 89, 88, 89, 87, 78, 63, 46, 40, 51, 81, 125, 169
        db 202, 216, 211, 196, 180, 169, 167, 168, 168, 163, 152, 141, 132, 128, 128, 127
        db 122, 112, 100, 91, 88, 88, 89, 85, 73, 56, 42, 42, 59, 94, 138, 180
        db 207, 216, 208, 192, 177, 168, 167, 168, 168, 162, 151, 140, 132, 128, 128, 127
        db 122, 112, 101, 92, 88, 88, 89, 86, 75, 59, 44, 40, 54, 85, 127, 170
        db 201, 215, 212, 198, 182, 171, 167, 168, 168, 165, 156, 145, 135, 129, 128, 128
        db 125, 118, 108, 97, 90, 88, 89, 89, 83, 71, 55, 42, 42, 59, 92, 135
        db 176, 204, 216, 211, 196, 181, 170, 167, 168, 168, 165, 157, 146, 136, 130, 128
        db 128, 126, 120, 110, 99, 91, 88, 89, 89, 86, 75, 60, 46, 40, 51, 78
        db 117, 159, 193, 212, 215, 204, 188, 175, 168, 167, 168, 167, 162, 153, 142, 133
        db 129, 128, 128, 125, 118, 108, 97, 90, 88, 89, 89, 85, 74, 59, 45, 41
        db 51, 77, 116, 157, 191, 211, 215, 206, 191, 177, 168, 166, 168, 168, 164, 156
        db 145, 136, 130, 128, 128, 127, 121, 112, 102, 93, 88, 88, 90, 88, 81, 68
        db 53, 42, 43, 59, 90, 129, 169, 199, 214, 213, 202, 187, 174, 168, 166, 168
        db 168, 163, 155, 144, 135, 130, 128, 128, 127, 122, 113, 103, 94, 89, 88, 89
        db 89, 83, 71, 57, 44, 41, 53, 79, 116, 155, 189, 210, 215, 208, 194, 180
        db 170, 166, 167, 168, 166, 160, 150, 140, 133, 129, 128, 128, 126, 120, 110, 100
        db 92, 88, 88, 90, 89, 82, 70, 56, 44, 42, 53, 79, 115, 154, 187, 208
        db 215, 209, 196, 182, 171, 167, 167, 168, 167, 162, 153, 143, 135, 130, 128, 128
        db 127, 123, 115, 105, 96, 90, 88, 89, 90, 87, 78, 65, 51, 42, 44, 59
        db 88, 125, 162, 193, 211, 215, 207, 194, 180, 170, 166, 167, 168, 167, 162, 154
        db 144, 135, 130, 128, 128, 127, 124, 117, 107, 98, 91, 88, 89, 90, 88, 82
        db 70, 56, 45, 42, 51, 74, 107, 144, 178, 202, 214, 212, 202, 188, 176, 168
        db 166, 167, 168, 166, 160, 151, 142, 134, 130, 128, 128, 127, 123, 116, 107, 98
        db 91, 88, 89, 90, 89, 83, 72, 58, 46, 42, 48, 67, 98, 134, 168, 196
        db 211, 214, 207, 194, 180, 171, 166, 166, 167, 167, 164, 157, 147, 138, 132, 129
        db 128, 128, 126, 122, 114, 105, 96, 90, 88, 89, 90, 89, 82, 71, 58, 46
        db 42, 48, 66, 95, 130, 165, 193, 210, 214, 208, 196, 183, 173, 167, 166, 167
        db 168, 165, 160, 151, 142, 134, 130, 128, 128, 127, 125, 119, 110, 101, 94, 89
        db 88, 90, 90, 88, 80, 69, 56, 45, 42, 50, 69, 98, 133, 166, 193, 209
        db 214, 209, 197, 184, 173, 167, 166, 167, 168, 166, 161, 153, 144, 136, 131, 128
        db 128, 128, 126, 122, 114, 105, 97, 91, 89, 89, 90, 90, 85, 77, 65, 52
        db 44, 43, 54, 75, 105, 139, 171, 196, 210, 214, 208, 196, 184, 173, 167, 166
        db 167, 167, 166, 162, 155, 146, 138, 132, 129, 128, 128, 127, 123, 117, 109, 100
        db 93, 89, 89, 90, 90, 88, 82, 72, 59, 48, 42, 46, 59, 83, 114, 147
        db 177, 200, 212, 213, 206, 195, 182, 173, 167, 166, 166, 167, 166, 162, 155, 147
        db 139, 133, 129, 128, 128, 127, 125, 119, 112, 103, 96, 91, 89, 89, 90, 90
        db 86, 78, 66, 54, 45, 43, 50, 67, 93, 124, 156, 184, 203, 213, 212, 204
        db 193, 181, 172, 167, 166, 166, 167, 166, 162, 156, 148, 140, 133, 129, 128, 128
        db 128, 126, 121, 114, 106, 98, 92, 89, 89, 90, 90, 88, 83, 73, 61, 50
        db 43, 44, 55, 75, 102, 133, 164, 189, 206, 213, 211, 203, 191, 180, 171, 167
        db 165, 166, 167, 166, 163, 156, 149, 141, 134, 130, 128, 128, 128, 126, 123, 117
        db 109, 101, 94, 90, 89, 89, 91, 90, 86, 79, 68, 57, 47, 43, 47, 60
        db 81, 109, 140, 169, 192, 207, 213, 210, 202, 190, 179, 171, 167, 165, 166, 167
        db 166, 163, 158, 150, 142, 135, 131, 129, 128, 128, 127, 124, 119, 112, 105, 97
        db 92, 89, 89, 90, 91, 89, 84, 76, 65, 54, 46, 43, 49, 63, 85, 113
        db 143, 171, 193, 207, 213, 210, 202, 191, 180, 172, 167, 165, 166, 167, 167, 164
        db 159, 153, 145, 138, 132, 129, 128, 128, 128, 126, 122, 117, 109, 102, 95, 91
        db 89, 89, 90, 91, 89, 83, 75, 64, 53, 46, 44, 49, 63, 85, 112, 141
        db 168, 191, 206, 212, 211, 204, 194, 183, 174, 168, 165, 165, 166, 167, 166, 162
        db 156, 149, 142, 135, 131, 129, 128, 128, 127, 125, 121, 115, 108, 101, 95, 91
        db 89, 90, 91, 91, 89, 84, 75, 65, 55, 47, 44, 48, 60, 79, 104, 132
        db 180, 208, 210, 194, 176, 166, 166, 167, 162, 150, 137, 130, 128, 127, 122, 110
        db 97, 90, 90, 91, 85, 68, 50, 44, 63, 105, 156, 196, 212, 205, 186, 171
        db 165, 166, 166, 158, 146, 134, 129, 128, 127, 119, 107, 95, 90, 90, 91, 83
        db 66, 48, 45, 66, 109, 159, 197, 212, 204, 186, 171, 165, 166, 166, 159, 147
        db 135, 129, 128, 127, 121, 109, 97, 90, 90, 91, 86, 71, 52, 44, 57, 94
        db 142, 186, 209, 209, 193, 176, 166, 165, 167, 163, 153, 140, 131, 128, 128, 125
        db 116, 104, 93, 89, 91, 90, 82, 65, 48, 46, 65, 106, 155, 194, 211, 206
        db 190, 173, 165, 165, 166, 162, 152, 139, 131, 128, 128, 125, 116, 104, 93, 90
        db 91, 90, 83, 66, 50, 45, 61, 99, 147, 188, 210, 209, 194, 176, 167, 165
        db 166, 164, 155, 143, 133, 128, 128, 127, 120, 109, 97, 91, 90, 91, 88, 76
        db 58, 45, 49, 75, 118, 165, 199, 211, 204, 188, 172, 165, 165, 166, 162, 153
        db 141, 132, 128, 128, 126, 119, 108, 96, 90, 90, 91, 87, 75, 58, 46, 49
        db 74, 116, 162, 197, 211, 206, 190, 174, 166, 165, 166, 164, 155, 143, 133, 129
        db 128, 127, 122, 112, 100, 92, 90, 91, 90, 81, 65, 50, 45, 60, 94, 139
        db 180, 206, 211, 199, 183, 170, 165, 166, 166, 161, 151, 140, 131, 128, 128, 126
        db 120, 109, 98, 91, 90, 91, 90, 80, 64, 49, 46, 61, 95, 139, 180, 205
        db 211, 200, 184, 170, 165, 165, 166, 163, 153, 142, 133, 129, 128, 127, 122, 113
        db 101, 93, 90, 91, 91, 85, 71, 55, 45, 52, 77, 117, 161, 194, 210, 207
        db 193, 177, 167, 164, 166, 165, 160, 150, 139, 131, 128, 128, 126, 121, 110, 99
        db 92, 90, 91, 91, 84, 70, 54, 45, 52, 78, 118, 160, 194, 210, 208, 194
        db 178, 168, 164, 166, 166, 161, 152, 141, 132, 128, 128, 127, 123, 114, 103, 94
        db 90, 91, 92, 88, 77, 61, 48, 47, 63, 96, 138, 177, 203, 211, 203, 188
        db 174, 166, 164, 166, 165, 159, 149, 138, 131, 128, 128, 127, 122, 112, 102, 93
        db 90, 91, 92, 87, 76, 61, 48, 47, 62, 94, 135, 173, 201, 210, 205, 190
        db 176, 167, 164, 165, 166, 161, 152, 141, 133, 129, 128, 128, 124, 117, 106, 96
        db 91, 90, 92, 90, 83, 70, 55, 46, 51, 73, 108, 149, 184, 205, 210, 201
        db 186, 173, 166, 164, 166, 165, 160, 151, 140, 132, 129, 128, 128, 124, 116, 106
        db 97, 91, 90, 92, 91, 84, 72, 57, 47, 49, 67, 99, 139, 176, 201, 210
        db 205, 191, 177, 167, 164, 165, 166, 163, 155, 145, 136, 130, 128, 128, 127, 121
        db 113, 102, 94, 90, 91, 92, 90, 82, 68, 54, 46, 51, 72, 106, 145, 180
        db 203, 210, 204, 190, 176, 167, 164, 165, 166, 163, 156, 146, 137, 130, 128, 128
        db 127, 123, 115, 105, 96, 91, 90, 92, 91, 85, 73, 59, 48, 47, 61, 89
        db 126, 164, 193, 208, 208, 198, 184, 172, 165, 164, 165, 165, 161, 153, 143, 135
        db 130, 128, 128, 127, 122, 113, 104, 95, 91, 91, 92, 91, 85, 74, 60, 49
        db 47, 60, 86, 121, 158, 189, 206, 209, 201, 187, 174, 166, 164, 165, 166, 163
        db 156, 147, 138, 131, 128, 128, 128, 125, 118, 109, 100, 93, 91, 91, 92, 90
        db 82, 70, 56, 47, 49, 64, 93, 128, 164, 192, 207, 209, 200, 186, 174, 166
        db 164, 165, 165, 163, 157, 148, 139, 132, 129, 128, 128, 126, 120, 111, 102, 95
        db 91, 91, 92, 91, 86, 75, 62, 50, 47, 55, 77, 109, 145, 178, 200, 209
        db 206, 195, 181, 171, 165, 164, 165, 165, 162, 155, 146, 137, 131, 128, 128, 128
        db 125, 120, 111, 102, 95, 91, 91, 92, 92, 87, 77, 64, 52, 47, 53, 71
        db 101, 136, 169, 194, 207, 208, 199, 187, 174, 167, 164, 164, 165, 164, 159, 151
        db 142, 134, 130, 128, 128, 127, 124, 117, 109, 100, 94, 91, 91, 92, 91, 86
        db 76, 63, 52, 47, 53, 72, 100, 135, 167, 193, 207, 208, 201, 188, 176, 168
        db 164, 164, 165, 165, 161, 154, 145, 137, 131, 128, 128, 128, 126, 121, 113, 104
        db 97, 92, 91, 92, 92, 90, 83, 72, 59, 50, 48, 56, 77, 107, 140, 171
        db 195, 207, 208, 200, 188, 176, 168, 164, 164, 165, 165, 161, 155, 146, 138, 132
        db 129, 128, 128, 127, 123, 116, 108, 99, 94, 91, 91, 92, 92, 87, 78, 66
        db 55, 48, 50, 63, 87, 118, 151, 179, 199, 208, 207, 198, 185, 174, 167, 164
        db 164, 165, 165, 161, 155, 146, 138, 132, 129, 128, 128, 127, 124, 117, 109, 101
        db 95, 91, 91, 92, 92, 89, 82, 71, 59, 50, 48, 56, 74, 101, 133, 164
        db 189, 204, 208, 204, 193, 181, 171, 165, 163, 164, 165, 164, 160, 153, 145, 138
        db 132, 129, 128, 128, 127, 124, 118, 110, 102, 96, 92, 91, 92, 93, 91, 85
        db 75, 63, 53, 48, 52, 66, 89, 119, 150, 177, 197, 207, 207, 200, 188, 177
        db 169, 164, 163, 164, 165, 163, 159, 152, 144, 136, 131, 129, 128, 128, 127, 124
        db 118, 110, 102, 96, 92, 91, 92, 93, 91, 86, 77, 66, 55, 49, 49, 60
        db 80, 107, 137, 166, 189, 203, 208, 204, 195, 183, 173, 166, 163, 163, 165, 165
        db 162, 157, 150, 142, 135, 131, 128, 128, 128, 127, 124, 118, 110, 103, 96, 92
        db 91, 92, 93, 92, 88, 80, 69, 58, 50, 48, 56, 72, 96, 125, 154, 180
        db 198, 207, 207, 200, 190, 179, 170, 165, 163, 164, 165, 164, 161, 156, 148, 141
        db 134, 130, 128, 128, 128, 127, 123, 118, 111, 103, 97, 93, 91, 92, 93, 92
        db 89, 82, 72, 61, 52, 48, 52, 65, 85, 112, 141, 168, 190, 203, 208, 204
        db 196, 185, 175, 168, 164, 163, 164, 165, 164, 160, 154, 147, 140, 134, 130, 128
        db 128, 128, 127, 124, 118, 112, 104, 98, 93, 92, 92, 93, 93, 91, 85, 76
        db 65, 56, 49, 50, 58, 74, 98, 125, 153, 178, 196, 205, 207, 202, 193, 182
        db 173, 166, 163, 163, 164, 164, 163, 160, 154, 147, 140, 134, 130, 128, 128, 128
        db 127, 124, 120, 113, 106, 99, 95, 92, 92, 93, 93, 92, 88, 81, 71, 61
        db 52, 49, 52, 63, 82, 107, 134, 160, 183, 198, 206, 206, 201, 191, 181, 172
        db 166, 163, 163, 164, 164, 163, 160, 155, 148, 141, 135, 131, 129, 128, 128, 128
        db 126, 122, 116, 109, 102, 97, 93, 92, 92, 93, 93, 91, 86, 78, 68, 58
        db 51, 49, 54, 66, 85, 109, 136, 162, 183, 198, 206, 206, 201, 192, 182, 173
        db 167, 163, 163, 163, 164, 164, 161, 157, 151, 144, 138, 133, 129, 128, 128, 128
        db 127, 124, 120, 114, 107, 101, 96, 93, 92, 92, 93, 93, 91, 86, 78, 68
        db 59, 52, 49, 53, 64, 82, 105, 131, 156, 178, 195, 204, 207, 203, 196, 186
        db 177, 169, 165, 163, 163, 164, 164, 163, 160, 155, 149, 142, 136, 132, 129, 128
        db 128, 128, 127, 124, 120, 114, 107, 101, 96, 93, 92, 92, 93, 93, 92, 87
        db 80, 71, 62, 54, 50, 51, 59, 73, 93, 117, 142, 166, 185, 199, 206, 206
        db 201, 193, 184, 175, 168, 164, 163, 163, 164, 164, 163, 160, 155, 149, 143, 137
        db 132, 129, 128, 128, 128, 127, 125, 121, 116, 110, 104, 98, 94, 92, 92, 93
        db 94, 93, 91, 85, 78, 69, 60, 53, 50, 52, 61, 75, 95, 119, 143, 166
        db 185, 198, 205, 206, 202, 194, 185, 177, 169, 165, 163, 162, 163, 164, 164, 161
        db 157, 152, 146, 140, 135, 131, 129, 128, 128, 128, 127, 125, 121, 115, 109, 103
        db 98, 94, 92, 92, 93, 94, 93, 91, 87, 80, 71, 62, 55, 50, 51, 57
        db 69, 87, 108, 132, 155, 175, 191, 201, 206, 205, 199, 191, 183, 174, 168, 164
        db 162, 162, 163, 164, 163, 161, 157, 152, 146, 140, 135, 131, 129, 128, 128, 128
        db 127, 126, 122, 118, 112, 106, 100, 96, 93, 92, 92, 93, 94, 93, 90, 85
        db 78, 69, 61, 54, 51, 51, 58, 70, 87, 107, 130, 152, 173, 189, 200, 205
        db 205, 201, 194, 185, 177, 170, 165, 163, 162, 163, 163, 164, 163, 160, 156, 150
        db 145, 139, 134, 131, 129, 128, 128, 128, 127, 126, 122, 118, 112, 107, 101, 97
        db 94, 92, 92, 93, 94, 94, 92, 88, 81, 74, 65, 58, 52, 51, 53, 62
        db 75, 93, 113, 135, 157, 175, 190, 200, 205, 205, 201, 194, 185, 177, 171, 166
        db 163, 162, 162, 163, 164, 163, 161, 157, 153, 147, 141, 136, 132, 130, 128, 128
        db 128, 128, 127, 125, 121, 117, 112, 106, 101, 97, 94, 93, 93, 93, 94, 94
        db 92, 89, 83, 76, 68, 60, 54, 51, 52, 58, 68, 84, 102, 123, 144, 164
        db 181, 194, 202, 205, 204, 199, 192, 184, 176, 170, 165, 163, 162, 162, 163, 163
        db 163, 161, 158, 154, 149, 143, 138, 134, 131, 129, 128, 128, 128, 128, 126, 124
        db 120, 115, 110, 105, 100, 96, 94, 93, 93, 93, 94, 94, 92, 89, 84, 77
        db 69, 62, 56, 52, 51, 56, 64, 77, 94, 114, 134, 154, 172, 187, 197, 203
        db 205, 202, 197, 190, 182, 175, 169, 165, 163, 162, 162, 163, 163, 163, 161, 159
        db 155, 150, 144, 139, 135, 132, 129, 128, 128, 128, 128, 127, 125, 123, 119, 114
        db 104, 97, 93, 93, 94, 92, 85, 72, 59, 51, 57, 76, 108, 145, 177, 198
        db 205, 198, 186, 173, 164, 162, 163, 163, 160, 153, 144, 135, 130, 128, 128, 127
        db 123, 115, 105, 97, 93, 93, 94, 93, 87, 75, 62, 53, 54, 69, 97, 132
        db 166, 192, 204, 202, 192, 178, 167, 162, 162, 163, 162, 158, 150, 140, 133, 129
        db 128, 128, 126, 121, 112, 103, 96, 93, 93, 94, 93, 86, 74, 61, 52, 54
        db 70, 97, 132, 165, 191, 203, 202, 193, 179, 168, 163, 162, 163, 163, 159, 152
        db 143, 135, 130, 128, 128, 127, 123, 116, 107, 99, 94, 93, 94, 94, 90, 81
        db 68, 57, 52, 59, 78, 108, 143, 174, 195, 204, 201, 190, 177, 167, 162, 162
        db 163, 162, 158, 151, 142, 135, 130, 128, 128, 127, 124, 117, 108, 100, 95, 93
        db 94, 95, 92, 84, 72, 60, 52, 55, 70, 97, 129, 162, 188, 202, 203, 195
        db 183, 171, 164, 161, 162, 163, 161, 156, 148, 140, 133, 129, 128, 128, 127, 122
        db 115, 107, 99, 94, 93, 94, 95, 92, 84, 72, 60, 53, 55, 69, 94, 126
        db 158, 184, 200, 204, 197, 186, 174, 165, 162, 162, 163, 162, 158, 151, 143, 135
        db 130, 128, 128, 128, 125, 120, 112, 103, 97, 94, 93, 95, 94, 90, 81, 69
        db 58, 52, 57, 72, 98, 130, 161, 186, 200, 203, 197, 186, 174, 166, 162, 161
        db 163, 162, 159, 153, 144, 137, 131, 128, 128, 128, 126, 122, 115, 106, 99, 95
        db 93, 94, 95, 93, 86, 76, 64, 55, 53, 62, 81, 109, 140, 169, 190, 201
        db 201, 194, 182, 172, 164, 161, 161, 162, 161, 158, 152, 144, 137, 131, 129, 128
        db 128, 127, 123, 116, 108, 101, 96, 94, 95, 96, 95, 90, 81, 70, 60, 55
        db 59, 73, 96, 125, 154, 178, 194, 200, 197, 188, 177, 167, 161, 160, 160, 161
        db 160, 156, 150, 142, 135, 131, 128, 128, 128, 126, 123, 117, 109, 102, 98, 96
        db 96, 97, 96, 93, 85, 75, 65, 58, 59, 70, 89, 115, 143, 168, 187, 196
        db 197, 190, 180, 170, 163, 159, 159, 159, 160, 158, 153, 147, 140, 134, 130, 128
        db 128, 128, 126, 122, 116, 109, 103, 99, 97, 97, 98, 98, 94, 87, 78, 68
        db 61, 61, 69, 85, 109, 135, 160, 180, 192, 195, 191, 183, 173, 164, 159, 157
        db 158, 159, 158, 155, 150, 144, 137, 132, 129, 128, 128, 128, 126, 122, 116, 109
        db 103, 99, 98, 98, 99, 99, 96, 89, 80, 71, 64, 63, 70, 84, 105, 129
        db 153, 174, 187, 192, 191, 184, 175, 166, 160, 157, 156, 157, 157, 156, 152, 147
        db 140, 135, 131, 129, 128, 128, 127, 125, 121, 115, 109, 103, 100, 99, 99, 100
        db 100, 97, 91, 82, 74, 67, 66, 71, 83, 102, 124, 148, 168, 182, 189, 189
        db 184, 176, 167, 160, 156, 155, 156, 156, 156, 153, 149, 143, 138, 133, 130, 128
        db 128, 128, 127, 124, 120, 114, 109, 104, 101, 100, 101, 101, 101, 98, 92, 85
        db 77, 70, 68, 72, 82, 99, 120, 141, 161, 176, 185, 187, 184, 177, 169, 162
        db 157, 154, 154, 155, 155, 154, 151, 146, 141, 136, 131, 129, 128, 128, 128, 127
        db 124, 120, 114, 109, 105, 102, 101, 102, 103, 102, 100, 95, 88, 80, 74, 71
        db 73, 81, 95, 114, 134, 153, 169, 180, 185, 184, 178, 171, 164, 158, 154, 153
        db 153, 154, 154, 152, 149, 144, 139, 134, 131, 129, 128, 128, 128, 127, 124, 120
        db 115, 110, 106, 104, 103, 103, 104, 104, 102, 98, 92, 84, 78, 74, 74, 80
        db 91, 106, 125, 143, 160, 173, 180, 182, 179, 174, 166, 160, 155, 152, 152, 152
        db 153, 152, 151, 147, 143, 138, 134, 131, 129, 128, 128, 128, 127, 125, 121, 117
        db 112, 108, 105, 104, 104, 105, 105, 104, 101, 96, 90, 83, 78, 76, 79, 86
        db 98, 113, 131, 148, 162, 173, 178, 179, 176, 170, 164, 158, 153, 151, 150, 151
        db 151, 151, 150, 147, 143, 138, 134, 131, 129, 128, 128, 128, 127, 126, 123, 119
        db 115, 111, 108, 106, 105, 106, 106, 106, 105, 101, 96, 90, 84, 80, 79, 82
        db 90, 101, 116, 132, 147, 161, 170, 175, 176, 174, 169, 163, 157, 153, 150, 149
        db 149, 150, 150, 149, 147, 144, 140, 136, 132, 130, 128, 128, 128, 128, 127, 125
        db 122, 118, 115, 111, 108, 107, 107, 107, 107, 107, 106, 103, 98, 93, 87, 83
        db 82, 84, 90, 100, 113, 128, 142, 155, 165, 171, 174, 172, 168, 163, 158, 153
        db 150, 148, 148, 148, 149, 148, 147, 145, 142, 138, 135, 132, 129, 128, 128, 128
        db 128, 127, 125, 123, 119, 116, 113, 110, 108, 108, 108, 109, 109, 108, 106, 102
        db 97, 92, 88, 85, 85, 89, 96, 106, 119, 132, 145, 156, 164, 169, 171, 169
        db 165, 160, 155, 151, 148, 147, 147, 147, 147, 147, 146, 144, 142, 138, 135, 132
        db 130, 129, 128, 128, 128, 128, 126, 124, 122, 119, 115, 113, 111, 109, 109, 110
        db 110, 110, 109, 107, 103, 99, 94, 90, 88, 88, 91, 98, 107, 118, 130, 142
        db 152, 160, 165, 167, 167, 164, 160, 155, 151, 148, 146, 145, 145, 146, 146, 146
        db 144, 142, 140, 137, 134, 131, 130, 128, 128, 128, 128, 128, 127, 125, 122, 120
        db 117, 114, 112, 111, 111, 111, 111, 111, 111, 109, 107, 103, 99, 95, 92, 91
        db 92, 96, 102, 111, 121, 132, 143, 152, 159, 163, 164, 164, 161, 157, 153, 150
        db 147, 145, 144, 144, 144, 144, 144, 143, 142, 140, 137, 134, 132, 130, 129, 128
        db 128, 128, 128, 127, 126, 124, 122, 120, 117, 115, 113, 112, 112, 112, 113, 113
        db 112, 111, 109, 106, 102, 98, 96, 94, 95, 97, 103, 110, 118, 128, 137, 146
        db 153, 158, 161, 161, 160, 157, 154, 150, 147, 145, 143, 142, 142, 143, 143, 143
        db 142, 141, 139, 136, 134, 132, 130, 129, 128, 128, 128, 128, 128, 127, 126, 124
        db 122, 119, 117, 115, 114, 114, 114, 114, 114, 114, 114, 112, 110, 107, 104, 101
        db 99, 97, 98, 100, 104, 111, 118, 126, 135, 142, 149, 154, 157, 158, 157, 155
        db 153, 149, 146, 144, 142, 141, 141, 141, 141, 142, 141, 140, 139, 137, 135, 133
        db 131, 130, 129, 128, 128, 128, 128, 128, 127, 126, 125, 123, 121, 119, 117, 116
        db 115, 115, 115, 116, 116, 116, 115, 113, 111, 109, 106, 103, 102, 101, 101, 103
        db 107, 112, 119, 126, 133, 140, 146, 150, 153, 155, 154, 153, 151, 148, 146, 143
        db 141, 140, 140, 140, 140, 140, 140, 140, 139, 138, 136, 134, 133, 131, 130, 129
        db 128, 128, 128, 128, 128, 127, 127, 125, 124, 122, 121, 119, 118, 117, 117, 117
        db 117, 117, 117, 117, 116, 115, 113, 111, 108, 106, 105, 104, 105, 106, 110, 114
        db 120, 125, 132, 137, 143, 147, 150, 151, 151, 150, 149, 147, 144, 142, 140, 139
        db 138, 138, 138, 138, 138, 138, 138, 137, 136, 135, 133, 132, 131, 129, 129, 128
        db 128, 128, 128, 128, 128, 127, 126, 125, 124, 122, 121, 120, 119, 118, 118, 118
        db 119, 119, 119, 118, 118, 116, 115, 113, 111, 109, 108, 107, 108, 109, 112, 115
        db 120, 125, 130, 135, 139, 143, 145, 147, 148, 148, 146, 145, 143, 141, 140, 138
        db 137, 137, 136, 137, 137, 137, 137, 136, 136, 135, 134, 133, 131, 130, 129, 129
        db 128, 128, 128, 128, 128, 128, 127, 127, 126, 125, 124, 123, 122, 121, 120, 120
        db 120, 120, 120, 120, 120, 120, 119, 119, 117, 116, 114, 113, 112, 111, 111, 112
        db 114, 116, 119, 123, 127, 131, 135, 138, 141, 143, 144, 144, 144, 143, 142, 140
        db 139, 137, 136, 136, 135, 135, 135, 135, 135, 135, 135, 135, 134, 133, 132, 131
        db 130, 130, 129, 128, 128, 128, 128, 128, 128, 128, 128, 127, 126, 126, 125, 124
        db 123, 123, 122, 122, 122, 122, 122, 122, 122, 122, 122, 121, 120, 119, 118, 117
        db 116, 115, 115, 115, 116, 117, 119, 121, 124, 127, 130, 133, 135, 137, 139, 140
        db 140, 140, 140, 139, 138, 137, 136, 135, 134, 134, 133, 133, 133, 133, 133, 133
        db 133, 133, 133, 132, 131, 131, 130, 129, 129, 128, 128, 128, 128, 128, 128, 128
        db 128, 128, 127, 127, 126, 126, 125, 124, 124, 124, 123, 123, 123, 123, 124, 124
        db 124, 123, 123, 123, 122, 121, 120, 120, 119, 119, 119, 119, 120, 121, 122, 124
        db 126, 128, 130, 132, 133, 135, 136, 136, 137, 137, 136, 136, 135, 134, 134, 133
        db 132, 132, 132, 132, 132, 132, 132, 132, 132, 131, 131, 131, 131, 130, 130, 129
        db 129, 129, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 127, 127, 127, 126
        db 126, 126, 125, 125, 125, 125, 125, 125, 125, 125, 125, 125, 125, 125, 124, 124
        db 123, 123, 123, 122, 122, 123, 123, 123, 124, 125, 126, 127, 128, 129, 130, 131
        db 132, 132, 133, 133, 133, 133, 132, 132, 131, 131, 131, 130, 130, 130, 130, 130
        db 130, 130, 130, 130, 130, 130, 130, 129, 129, 129, 129, 129, 128, 128, 128, 128
        db 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 127, 127, 127, 127, 127
        db 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 126
        db 126, 126, 126, 127, 127, 127, 127, 127, 128, 128, 128, 128, 128, 129, 129, 129
        db 129, 129, 129, 129, 129, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128
; CLASSIC
maze1:  db "############################"
        db "#..........................#"
        db "#.####.#####.##.#####.####.#"
        db "#o####.#####.##.#####.####o#"
        db "#.####.#####.##.#####.####.#"
        db "#............##............#"
        db "#.####.##.########.##.####.#"
        db "#.####.##.########.##.####.#"
        db "#......##....##....##......#"
        db "######.##### ## #####.######"
        db "######.##### ## #####.######"
        db "    ##.##          ##.##    "
        db "######.## ###--### ##.######"
        db "######.## #      # ##.######"
        db "1     .   #   H  #   .     1"
        db "######.## #      # ##.######"
        db "######.## ######## ##.######"
        db "    ##.##          ##.##    "
        db "######.## ######## ##.######"
        db "######.## ######## ##.######"
        db "#............##............#"
        db "#.####.#####.##.#####.####.#"
        db "#.####.#####.##.#####.####.#"
        db "#o..##.......PP.......##..o#"
        db "###.##.##.########.##.##.###"
        db "###.##.##.########.##.##.###"
        db "#......##....##....##......#"
        db "#.##########.##.##########.#"
        db "#.##########.##.##########.#"
        db "#..........................#"
        db "############################"

; Parking Lot
maze2:  db "############################"
        db "1      ##..........##      1"
        db "###### ##.########.## ######"
        db "###### ##.########.## ######"
        db "#......##..........##......#"
        db "#o####.##.########.##.####o#"
        db "#.####.##.########.##.####.#"
        db "#..........................#"
        db "#########.########.#########"
        db "#########.########.#########"
        db "#.........        .........#"
        db "#.##.#### ###--### ####.##.#"
        db "#.##.#### #      # ####.##.#"
        db "#.##.     #   H  #     .##.#"
        db "#o##.#### #      # ####.##o#"
        db "#.##.#### ######## ####.##.#"
        db "#.........        .........#"
        db "#########.########.#########"
        db "#########.########.#########"
        db "#..........................#"
        db "#.########################.#"
        db "#.########################.#"
        db "2............PP............2"
        db "#########.##.##.##.#########"
        db "#########.##.##.##.#########"
        db "#..........................#"
        db "#.###.##.###.##.###.##.###.#"
        db "#o###.##.###.##.###.##.###o#"
        db "#.###.##.###.##.###.##.###.#"
        db "#..........................#"
        db "############################"

; Metro Station
maze3:  db "                            "
        db "                            "
        db "                            "
        db "######     ######     ######"
        db "#....# ### #....# ### #....#"
        db "#o##.#######.##.#######.##o#"
        db "#.##.........##.........##.#"
        db "#....####.##.##.##.####....#"
        db "###.#####.##....##.#####.###"
        db "###.#####.########.#####.###"
        db "#.........########.........#"
        db "1.####.##....PP....##.####.1"
        db "#.####.##.########.##.####.#"
        db "#...##.##.########.##.##...#"
        db "###.##.##..........##.##.###"
        db " ##.##.##### ## #####.##.## "
        db "###.##.##### ## #####.##.###"
        db "#......              ......#"
        db "#.####### ######## #######.#"
        db "#.####### #      # #######.#"
        db "#.#....## #   H  # ##....#.#"
        db "#2#.##.## #      # ##.##.#2#"
        db "###.##.## ###--### ##.##.###"
        db "#.........        .........#"
        db "#.##.#.##.########.##.#.##.#"
        db "#o##.#.##.########.##.#.##o#"
        db "#....#................#....#"
        db "############################"
        db "                            "
        db "                            "
        db "                            "

; Photo Opportunity
maze4:  db "                            "
        db "                            "
        db "  ###      ######      ###  "
        db " ###########################"
        db "##........................ 2"
        db "##.###.#.###.##.###.#.###.##"
        db "##o###.#.###.##.###.#.###o##"
        db " #.....#.....##.....#.....# "
        db " #.#####.##########.#####.# "
        db " #........................# "
        db " #.######.########.######.# "
        db " #.....##..........##.....# "
        db " #.###.##.########.##.###.# "
        db " #.....##          ##.....# "
        db "######.## ###--### ##.######"
        db "######.## #      # ##.######"
        db "1     ....#   H  #....     1"
        db "######.##.#      #.##.######"
        db "######.##.########.##.######"
        db " #...........PP...........# "
        db " #.###.##.##.##.##.##.###.# "
        db " #.###.##.##.##.##.##.###.# "
        db " #.....##.##.##.##.##.....# "
        db " #.###.##.##....##.##.###.# "
        db " #.###....########....###.# "
        db "##o###.##.##....##.##.###o##"
        db "##.###.##.##.##.##.##.###.##"
        db "2 ........................##"
        db "########################### "
        db "  ###      ######      ###  "
        db "                            "

; --- the per-level difficulty table, the arcade's own figures. Generated from
; LEVEL_TABLE in pacman-src.ts, which is checked against the table in
; docs/pacman-design.md by a test, so neither can drift from the other.
lvlrom: db 205, 192, 128, 45, 20, 52, 102
        db 230, 218, 141, 38, 30, 52, 115
        db 230, 218, 141, 30, 40, 52, 115
        db 230, 218, 141, 22, 40, 52, 115
        db 255, 243, 154, 15, 40, 38, 128
        db 255, 243, 154, 38, 50, 38, 128
        db 255, 243, 154, 15, 50, 38, 128
        db 255, 243, 154, 15, 50, 38, 128
        db 255, 243, 154, 8, 60, 38, 128
        db 255, 243, 154, 38, 60, 38, 128
        db 255, 243, 154, 15, 60, 38, 128
        db 255, 243, 154, 8, 80, 38, 128
        db 255, 243, 154, 8, 80, 38, 128
        db 255, 243, 154, 22, 80, 38, 128
        db 255, 243, 154, 8, 100, 38, 128
        db 255, 243, 154, 8, 100, 38, 128
        db 255, 243, 154, 0, 100, 38, 128
        db 255, 243, 154, 8, 100, 38, 128
        db 255, 243, 154, 0, 120, 38, 128
        db 255, 243, 154, 0, 120, 38, 128
        db 230, 243, 154, 0, 120, 38, 128

; --- the score strip's template. The gaps are sized for the longest strip,
; a five digit score on a three digit level, which is 41 of the 42 columns.
; See hud.
hudfmt: db "SCORE %04u0        LIVES %hhu     LEVEL %hhu", 0
