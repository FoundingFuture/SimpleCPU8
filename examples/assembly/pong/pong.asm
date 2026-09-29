; Pong: CMD_HIT_TEST, which asks about two sprites by name.
;
; The other collision commands search. CMD_HIT_SCAN looks through every
; sprite, and the group commands ask about kinds of thing. This one asks the
; only question a game of pong ever has: is the ball touching THAT paddle.
; Two sprites, named, one answer. Two of those a frame is the whole game.
;
; Up and down move the left paddle. The right one follows the ball.
;
; The answer comes back on GPU_HIT, and IN sets no CPU flag, so every read
; below is followed by an AND before anything branches on it.

        OUT GPU_COLOR, 0
        OUT GPU_CMD, CMD_CLEAR
        OUT GPU_TEXT_COLOR, $FF
        OUT GPU_CMD, CMD_TEXT_STYLE

; ---- three sprites. No groups: this demo is about naming them. ----
        OUT GPU_SPRITE, 1
        OUT GPU_SRC_BANK, get_bankbyte(ballspr)
        OUT GPU_SRC_HI, get_highbyte(ballspr)
        OUT GPU_SRC_LO, get_lowbyte(ballspr)
        OUT GPU_CMD, CMD_SPRITE_DEF
        OUT GPU_SPRITE, 1
        OUT GPU_CMD, CMD_SPRITE_SHOW

        OUT GPU_SPRITE, 2
        OUT GPU_SRC_BANK, get_bankbyte(lpspr)
        OUT GPU_SRC_HI, get_highbyte(lpspr)
        OUT GPU_SRC_LO, get_lowbyte(lpspr)
        OUT GPU_CMD, CMD_SPRITE_DEF
        OUT GPU_SPRITE, 2
        OUT GPU_CMD, CMD_SPRITE_SHOW

        OUT GPU_SPRITE, 3
        OUT GPU_SRC_BANK, get_bankbyte(rpspr)
        OUT GPU_SRC_HI, get_highbyte(rpspr)
        OUT GPU_SRC_LO, get_lowbyte(rpspr)
        OUT GPU_CMD, CMD_SPRITE_DEF
        OUT GPU_SPRITE, 3
        OUT GPU_CMD, CMD_SPRITE_SHOW

; ---- one beep, three pitches ----
; A sample is defined once with a root note. CMD_TRIGGER then plays it at
; whatever note is asked for, and the chip shifts it: up an octave for the
; paddle blip, the root for a wall, and low for a miss.
        OUT APU_CART_BANK, get_bankbyte(beepfx)
        OUT APU_CART_HI, get_highbyte(beepfx)
        OUT APU_CART_LO, get_lowbyte(beepfx)
        OUT APU_ARG, 1
        OUT APU_ARG2, 64
        OUT APU_NOTE, 60
        OUT APU_SLOT, 0
        OUT APU_CMD, CMD_DEF_SAMPLE

; The paddles never move sideways, so their x is written once.
        OUT GPU_SPRITE, 2
        OUT GPU_SPRITE_X_HI, 0
        OUT GPU_SPRITE_X, 8
        OUT GPU_SPRITE_Y_HI, 0
        OUT GPU_SPRITE_Y, 110
        OUT GPU_CMD, CMD_SPRITE_MOVE

loop:   JSR pad
        JSR ai
        JSR ball
        JSR bounce
        JSR draw
        JSR hud
        JSR waitframe
        JMP loop

; --- the player's paddle, clamped to the court.
pad:    IN IO_CONTROLLER -> A
        LD [pd] <- A
        TST A, BTN_UP
        JZ pnu
        LD A <- [ly]
        JZ pnu                         ; already at the top
        SUB A <- 2
        LD [ly] <- A
pnu:    LD A <- [pd]
        TST A, BTN_DOWN
        JZ pnd
        LD A <- [ly]
        CMP A, 220
        JNC pnd
pdok:   ADD A <- 2                     ; CMP kept ly in A
        LD [ly] <- A
pnd:    RET

; --- the other paddle follows the ball, one pixel a frame, which is slow
; enough to beat and fast enough to be worth playing.
; SUB sets C as the borrow, so a borrow here means the ball is above.
ai:     LD A <- [by]
        CMP A, [ry]
        JC aiup
        LD A <- [ry]
        CMP A, 220
        JC aidn
        RET
aidn:   INC A
        LD [ry] <- A
        RET
aiup:   LD A <- [ry]
        JZ airet
        SUB A <- 1
        LD [ry] <- A
airet:  RET

; --- where on the paddle it landed, and what that does to the ball.
;
; The middle plays as it always did. Out toward an end it leaves steeper and
; faster, which is the whole of pong's skill: a player who can only return
; the ball is beaten by one who can place it.
;
;   off = ball centre - paddle centre = by + 3 - (paddle + 18)
;
; A signed byte, so bit 7 is the side of the centre it hit. There is no
; negate instruction: the magnitude of a negative byte is its bits inverted,
; plus one.
angle:  LD A <- [by]
        SUB A <- [pyt]
        SUB A <- 15
        TST A, $80                     ; TST keeps the offset in A
        JZ adown
        XOR A <- $FF                   ; above the centre: it leaves upward,
        INC A                          ; and the magnitude is the negative
        LD [amag] <- A
        LD A <- 1
        LD [aup] <- A
        JMP azone
adown:  LD [amag] <- A
        LD A <- 0
        LD [aup] <- A
azone:  LD A <- [amag]
        CMP A, 7
        JC amid
        CMP A, 15                      ; CMP kept amag in A
        JC aouter
        LD A <- 3                      ; the very end: steepest and fastest
        LD [ady] <- A
        LD [adx] <- A
        JMP aset
aouter: LD A <- 2
        LD [ady] <- A
        LD [adx] <- A
        JMP aset
amid:   LD A <- 1                      ; the middle, as before
        LD [ady] <- A
        LD A <- 2
        LD [adx] <- A
aset:   LD A <- [aup]
        JZ adyp
        LD A <- 0
        SUB A <- [ady]
        LD [bdy] <- A
        JMP adxs
adyp:   LD A <- [ady]
        LD [bdy] <- A
adxs:   LD A <- [xdir]
        JZ adxn
        LD A <- [adx]
        LD [bdx] <- A
        RET
adxn:   LD A <- 0
        SUB A <- [adx]
        LD [bdx] <- A
        RET

; --- the ball, and the walls. A byte wraps, so a ball that walks off either
; end of an axis lands above the court and one test catches both.
ball:   LD A <- [by]
        ADD A <- [bdy]
        LD [by] <- A
        CMP A, 251
        JC bxmove
        LD A <- 0                      ; off the top or the bottom: turn round
        SUB A <- [bdy]
        LD [bdy] <- A
        ADD A <- [by]                  ; and undo the step that left the court:
        ADD A <- [bdy]                 ; twice the new step, the other way
        LD [by] <- A
        OUT APU_TRACK, 1               ; a wall, on its own track so a paddle
        OUT APU_SLOT, 0                ; blip and a wall blip do not cut
        OUT APU_NOTE, 60
        OUT APU_ARG, 100
        OUT APU_CMD, CMD_TRIGGER
bxmove: LD A <- [bx]
        ADD A <- [bdx]
        LD [bx] <- A
        RET

; --- the two questions. Each names both sprites, and each answers one bit.
; DESIGN: a hit SETS the direction rather than negating it. A ball still
; overlapping on the next frame then asks for the same direction it already
; has, so it leaves. Negating would trap it inside the paddle, flipping every
; frame while it went nowhere.
bounce: OUT GPU_SPRITE, 1
        OUT GPU_SPRITE_B, 2
        OUT GPU_CMD, CMD_HIT_TEST
        IN GPU_HIT -> A
        TST A, $FF                   ; IN sets no CPU flag, so mask first
        JZ bnol
        LD A <- [ly]                   ; where on the paddle it landed
        LD [pyt] <- A
        LD A <- 1                      ; and it leaves to the right
        LD [xdir] <- A
        JSR angle
        OUT APU_TRACK, 0
        OUT APU_SLOT, 0
        OUT APU_NOTE, 72
        OUT APU_ARG, 110               ; velocity
        OUT APU_CMD, CMD_TRIGGER
bnol:   OUT GPU_SPRITE, 1
        OUT GPU_SPRITE_B, 3
        OUT GPU_CMD, CMD_HIT_TEST
        IN GPU_HIT -> A
        TST A, $FF
        JZ bnor
        LD A <- [ry]
        LD [pyt] <- A
        LD A <- 0                      ; and it leaves to the left
        LD [xdir] <- A
        JSR angle
        OUT APU_TRACK, 0
        OUT APU_SLOT, 0
        OUT APU_NOTE, 72
        OUT APU_ARG, 110
        OUT APU_CMD, CMD_TRIGGER
bnor:
; Past a paddle is a point. The direction it was travelling says whose.
        LD A <- [bx]
        CMP A, 250
        JC bdone
        LD A <- [bdx]
        TST A, $80                   ; the sign: set means it went left
        JZ pscore
        LD A <- [cpu]
        ADD A <- 1
        LD [cpu] <- A
        LD A <- 2                      ; serve toward the player's side
        LD [bdx] <- A
        JMP serve
pscore: LD A <- [you]
        ADD A <- 1
        LD [you] <- A
        LD A <- 254
        LD [bdx] <- A
serve:  OUT APU_TRACK, 2               ; a miss: the same beep, pitched down,
        OUT APU_SLOT, 0                ; which makes it the long low one
        OUT APU_NOTE, 45
        OUT APU_ARG, 120
        OUT APU_CMD, CMD_TRIGGER
        LD A <- 124
        LD [bx] <- A
        LD A <- 60
        LD [by] <- A
bdone:  RET

draw:   OUT GPU_SPRITE, 1
        OUT GPU_SPRITE_X_HI, 0
        LD A <- [bx]
        OUTA GPU_SPRITE_X
        OUT GPU_SPRITE_Y_HI, 0
        LD A <- [by]
        OUTA GPU_SPRITE_Y
        OUT GPU_CMD, CMD_SPRITE_MOVE
        OUT GPU_SPRITE, 2
        OUT GPU_SPRITE_X_HI, 0
        OUT GPU_SPRITE_X, 8
        OUT GPU_SPRITE_Y_HI, 0
        LD A <- [ly]
        OUTA GPU_SPRITE_Y
        OUT GPU_CMD, CMD_SPRITE_MOVE
        OUT GPU_SPRITE, 3
        OUT GPU_SPRITE_X_HI, 0
        OUT GPU_SPRITE_X, 242
        OUT GPU_SPRITE_Y_HI, 0
        LD A <- [ry]
        OUTA GPU_SPRITE_Y
        OUT GPU_CMD, CMD_SPRITE_MOVE
        RET

hud:    OUT GPU_CMD, CMD_TEXT_CLEAR
        OUT GPU_CART_BANK, get_bankbyte(fmt)
        OUT GPU_CART_HI, get_highbyte(fmt)
        OUT GPU_CART_LO, get_lowbyte(fmt)
        OUT GPU_TEXT_ARG_HI, args >> 8
        OUT GPU_TEXT_ARG_LO, args & 255
        OUT GPU_CMD, CMD_PRINTF
        RET

waitframe: IN GPU_FRAME -> A
        CMP A, [flast]
        JZ waitframe
        LD [flast] <- A           ; CMP kept the frame in A
        RET

.ram
bx:     db 124
by:     db 60
bdx:    db 2
bdy:    db 1
; The bounce angle's working: the paddle it hit, which way it leaves, and the
; offset from that paddle's centre with its sign taken off.
pyt:    db 0
xdir:   db 0
amag:   db 0
aup:    db 0
ady:    db 0
adx:    db 0
ly:     db 110
ry:     db 110
pd:     db 0
; The two scores ARE the printf arguments, in the order the template wants
; them, so nothing is copied into a separate block each frame.
args:
you:    db 0
cpu:    db 0
flast:  db 0

.data
fmt:    db "YOU %hhu    CPU %hhu", 0

ballspr: db 1, 6, 6
        db $FF, $FF, $FF, $FF, $FF, $FF
        db $FF, $FF, $FF, $FF, $FF, $FF
        db $FF, $FF, $FF, $FF, $FF, $FF
        db $FF, $FF, $FF, $FF, $FF, $FF
        db $FF, $FF, $FF, $FF, $FF, $FF
        db $FF, $FF, $FF, $FF, $FF, $FF
lpspr:  db 1, 6, 36
        db $1F, $1F, $1F, $1F, $1F, $1F
        db $1F, $1F, $1F, $1F, $1F, $1F
        db $1F, $1F, $1F, $1F, $1F, $1F
        db $1F, $1F, $1F, $1F, $1F, $1F
        db $1F, $1F, $1F, $1F, $1F, $1F
        db $1F, $1F, $1F, $1F, $1F, $1F
        db $1F, $1F, $1F, $1F, $1F, $1F
        db $1F, $1F, $1F, $1F, $1F, $1F
        db $1F, $1F, $1F, $1F, $1F, $1F
        db $1F, $1F, $1F, $1F, $1F, $1F
        db $1F, $1F, $1F, $1F, $1F, $1F
        db $1F, $1F, $1F, $1F, $1F, $1F
        db $1F, $1F, $1F, $1F, $1F, $1F
        db $1F, $1F, $1F, $1F, $1F, $1F
        db $1F, $1F, $1F, $1F, $1F, $1F
        db $1F, $1F, $1F, $1F, $1F, $1F
        db $1F, $1F, $1F, $1F, $1F, $1F
        db $1F, $1F, $1F, $1F, $1F, $1F
        db $1F, $1F, $1F, $1F, $1F, $1F
        db $1F, $1F, $1F, $1F, $1F, $1F
        db $1F, $1F, $1F, $1F, $1F, $1F
        db $1F, $1F, $1F, $1F, $1F, $1F
        db $1F, $1F, $1F, $1F, $1F, $1F
        db $1F, $1F, $1F, $1F, $1F, $1F
        db $1F, $1F, $1F, $1F, $1F, $1F
        db $1F, $1F, $1F, $1F, $1F, $1F
        db $1F, $1F, $1F, $1F, $1F, $1F
        db $1F, $1F, $1F, $1F, $1F, $1F
        db $1F, $1F, $1F, $1F, $1F, $1F
        db $1F, $1F, $1F, $1F, $1F, $1F
        db $1F, $1F, $1F, $1F, $1F, $1F
        db $1F, $1F, $1F, $1F, $1F, $1F
        db $1F, $1F, $1F, $1F, $1F, $1F
        db $1F, $1F, $1F, $1F, $1F, $1F
        db $1F, $1F, $1F, $1F, $1F, $1F
        db $1F, $1F, $1F, $1F, $1F, $1F
rpspr:  db 1, 6, 36
        db $E0, $E0, $E0, $E0, $E0, $E0
        db $E0, $E0, $E0, $E0, $E0, $E0
        db $E0, $E0, $E0, $E0, $E0, $E0
        db $E0, $E0, $E0, $E0, $E0, $E0
        db $E0, $E0, $E0, $E0, $E0, $E0
        db $E0, $E0, $E0, $E0, $E0, $E0
        db $E0, $E0, $E0, $E0, $E0, $E0
        db $E0, $E0, $E0, $E0, $E0, $E0
        db $E0, $E0, $E0, $E0, $E0, $E0
        db $E0, $E0, $E0, $E0, $E0, $E0
        db $E0, $E0, $E0, $E0, $E0, $E0
        db $E0, $E0, $E0, $E0, $E0, $E0
        db $E0, $E0, $E0, $E0, $E0, $E0
        db $E0, $E0, $E0, $E0, $E0, $E0
        db $E0, $E0, $E0, $E0, $E0, $E0
        db $E0, $E0, $E0, $E0, $E0, $E0
        db $E0, $E0, $E0, $E0, $E0, $E0
        db $E0, $E0, $E0, $E0, $E0, $E0
        db $E0, $E0, $E0, $E0, $E0, $E0
        db $E0, $E0, $E0, $E0, $E0, $E0
        db $E0, $E0, $E0, $E0, $E0, $E0
        db $E0, $E0, $E0, $E0, $E0, $E0
        db $E0, $E0, $E0, $E0, $E0, $E0
        db $E0, $E0, $E0, $E0, $E0, $E0
        db $E0, $E0, $E0, $E0, $E0, $E0
        db $E0, $E0, $E0, $E0, $E0, $E0
        db $E0, $E0, $E0, $E0, $E0, $E0
        db $E0, $E0, $E0, $E0, $E0, $E0
        db $E0, $E0, $E0, $E0, $E0, $E0
        db $E0, $E0, $E0, $E0, $E0, $E0
        db $E0, $E0, $E0, $E0, $E0, $E0
        db $E0, $E0, $E0, $E0, $E0, $E0
        db $E0, $E0, $E0, $E0, $E0, $E0
        db $E0, $E0, $E0, $E0, $E0, $E0
        db $E0, $E0, $E0, $E0, $E0, $E0
        db $E0, $E0, $E0, $E0, $E0, $E0

; A square wave at 440 Hz, 40 ms of it. 8 bit unsigned, silence at 128.
beepfx:
        db 224, 224, 224, 224, 224, 224, 224, 224, 224, 224, 32, 32, 32, 32, 32, 32
        db 32, 32, 32, 224, 224, 224, 224, 224, 224, 224, 224, 224, 32, 32, 32, 32
        db 32, 32, 32, 32, 32, 224, 224, 224, 224, 224, 224, 224, 224, 224, 32, 32
        db 32, 32, 32, 32, 32, 32, 32, 224, 224, 224, 224, 224, 224, 224, 224, 224
        db 32, 32, 32, 32, 32, 32, 32, 32, 32, 224, 224, 224, 224, 224, 224, 224
        db 224, 224, 32, 32, 32, 32, 32, 32, 32, 32, 32, 224, 224, 224, 224, 224
        db 224, 224, 224, 224, 224, 32, 32, 32, 32, 32, 32, 32, 32, 32, 224, 224
        db 224, 224, 224, 224, 224, 224, 224, 32, 32, 32, 32, 32, 32, 32, 32, 32
        db 224, 224, 224, 224, 224, 224, 224, 224, 224, 32, 32, 32, 32, 32, 32, 32
        db 32, 32, 224, 224, 224, 224, 224, 224, 224, 224, 224, 32, 32, 32, 32, 32
        db 32, 32, 32, 32, 224, 224, 224, 224, 224, 224, 224, 224, 224, 32, 32, 32
        db 32, 32, 32, 32, 32, 32, 224, 224, 224, 224, 224, 224, 224, 224, 224, 32
        db 32, 32, 32, 32, 32, 32, 32, 32, 32, 224, 224, 224, 224, 224, 224, 224
        db 224, 224, 32, 32, 32, 32, 32, 32, 32, 32, 32, 224, 224, 224, 224, 224
        db 224, 224, 224, 224, 32, 32, 32, 32, 32, 32, 32, 32, 32, 224, 224, 224
        db 224, 223, 222, 220, 219, 218, 39, 40, 42, 43, 44, 45, 46, 48, 49, 206
        db 205, 204, 202, 201, 200, 199, 198, 196, 61, 62, 63, 64, 66, 67, 68, 69
        db 70, 184, 183, 182, 181, 180, 178, 177, 176, 175, 82, 84, 85, 86, 87, 88
        db 90, 91, 92, 163, 162, 160, 159, 158, 157, 156, 154, 153, 152, 105, 106, 108
        db 109, 110, 111, 112, 114, 115, 140, 139, 138, 136, 135, 134, 133, 132, 130, 127
