; Matrix rain in text mode. The screen is 42x32 characters read from RAM
; at $FAC0, ending exactly at $FFFF. Each column has a falling head that
; writes a fresh random glyph, a tail eight cells back that erases, and a
; flicker pass mutates a few visible cells each frame. Green on black.

        OUT GPU_TEXT_COLOR, $1C        ; bright green
        OUT GPU_TEXT_BG, 0
        OUT GPU_CMD, CMD_TEXT_STYLE
        OUT GPU_ADDR_HI, $FA           ; map base $FAC0
        OUT GPU_ADDR_LO, $C0
        OUT GPU_CMD, CMD_SET_TEXTMODE

; Set up one record per column: head pointer, gap, speed.
        LD D2 <- records
        LD A <- 0
        LD [initc] <- A
        LD A <- 42
        LD [colcount] <- A
initloop:
        LD D1 <- $FAC0                 ; head = screen base plus the column
        LD A <- [initc]
        LD D1 <- D1+A                  ; the address adder does the 16 bit add
        LD [D2] <- D1                  ; record head pointer
        JSR rng
        AND A <- $1F
        LD [D2+2] <- A                 ; a random gap, so the columns stagger
        JSR rng
        AND A <- $01
        LD [D2+3] <- A                 ; speed = 0 or 1
        LD D2 <- D2+4                  ; the next record
        LD A <- [initc]
        ADD A <- 1
        LD [initc] <- A
        LD A <- [colcount]
        SUB A <- 1
        LD [colcount] <- A
        JNZ initloop
initdone:
        LD A <- 0
        LD [lastframe] <- A
        LD [parity] <- A

; One rain step per device frame.
main:
        IN GPU_FRAME
        CMP A, [lastframe]
        JZ main
        LD [lastframe] <- A          ; CMP kept the frame in A
        LD A <- [parity]
        XOR A <- 1
        LD [parity] <- A

        LD D2 <- records
        LD A <- 42
        LD [colcount] <- A
colloop:
        LD A <- [D2+3]                 ; speed
        JNZ docol
slowcol:
        LD A <- [parity]
        JNZ nextcol                       ; slow columns move on even frames only
docol:
        LD D1 <- [D2]                  ; head pointer
        LD [hp_hi] <- D1
        LD A <- [D2+2]                 ; gap
        JZ headrand
        SUB A <- 1
        LD [D2+2] <- A
        LD A <- $20                    ; inside a gap: draw a space
        JMP puthead
headrand:
        JSR randchar
puthead:
        LD [D1] <- A                   ; write at the head cell
        LD D1 <- D1-$150               ; tail = head minus eight rows
        LD [tp_hi] <- D1
        LD A <- [tp_lo]                ; below the base? add the screen back
        SUB A <- $C0
        LD A <- [tp_hi]
        SBC A <- $FA
        JNC tailok
tailwrap:
        LD D1 <- D1+$540
tailok:
        LD A <- $20
        LD [D1] <- A                   ; erase the tail cell
        LD D1 <- [hp_hi]               ; advance head one row, 42 cells
        LD D1 <- D1+42
        LD [hp_hi] <- D1
        LD A <- [hp_hi]                ; the high byte is 0 once it passes $FFFF
        JNZ storehead
headwrap:
        LD D1 <- D1+$FAC0              ; the screen ends at $FFFF, so the same
                                       ; offset from its base is where it goes
        JSR rng
        AND A <- $07
        LD [D2+2] <- A                 ; new gap on respawn
        JSR rng
        AND A <- $01
        LD [D2+3] <- A                 ; new speed on respawn
storehead:
        LD [D2] <- D1
nextcol:
        LD D2 <- D2+4
        LD A <- [colcount]
        SUB A <- 1
        LD [colcount] <- A
        JNZ colloop

flick:
        LD A <- 12
        LD [flickcnt] <- A
flickloop:
        JSR rng
        LD [fp_lo] <- A                ; a random offset into the screen,
        JSR rng                        ; low byte then high
        AND A <- $07                   ; 0..2047
        LD [fp_hi] <- A

; The screen is 1344 cells, which is not a power of two. Draw from 2048 and
; DROP a draw past the end. Folding it instead would land on the first 704
; cells twice as often, and the top of the screen would flicker harder.
        LD A <- [fp_lo]
        SUB A <- $40
        LD A <- [fp_hi]
        SBC A <- $05
        JNC flicknext                     ; borrowed, so the offset is under 1344

flickin:
        LD D1 <- [fp_hi]               ; cell = base + offset
        LD D1 <- D1+$FAC0
        LD A <- [D1]
        JZ flicknext                   ; blank cell: leave it
        CMP A, $20
        JZ flicknext                   ; space: leave it
        JSR randchar
        LD [D1] <- A
flicknext:
        LD A <- [flickcnt]
        SUB A <- 1
        LD [flickcnt] <- A
        JNZ flickloop
flickdone:
        JMP main

; 8-bit maximal-length Galois LFSR. SHL drops the bit shifted out into C,
; so the feedback XOR is one branch. Period 255.
rng:
        LD A <- [seed]
        SHL A
        JNC rngst
rngfb:
        XOR A <- $1D
rngst:
        LD [seed] <- A
        RET

; A random printable glyph, 0x21 to 0x60.
randchar:
        JSR rng
        AND A <- $3F
        ADD A <- $21
        RET

.ram
seed:      db $3F
parity:    db 0
colcount:  db 0
initc:     db 0
lastframe: db 0
flickcnt:  db 0
hp_hi:     db 0
hp_lo:     db 0
tp_hi:     db 0
tp_lo:     db 0
fp_hi:     db 0
fp_lo:     db 0
records:   db 0
