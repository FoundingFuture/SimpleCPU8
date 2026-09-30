; Sprite groups: asking the GPU about KINDS of thing.
;
; A game does not want to know that sprite 7 touched sprite 23. It wants to
; know that an enemy touched the player. So every sprite carries a mask of
; eight group bits, set once by CMD_SPRITE_DEF, and four commands ask
; questions in those terms.
;
; Arrow keys move the white dot. Everything else drifts and bounces.
;
;   group 0   the player, white
;   group 1   red
;   group 2   green
;   group 3   blue
;
; All four group commands are on screen at once:
;
;   CMD_COLLIDE_GROUP_ALL  every dot that is touching anything lights up.
;                          One command a frame for all twelve, and then one
;                          load each, instead of a command each.
;   CMD_SPRITE_HITS        the PLAYER line: which groups you are touching,
;                          as the eight bits themselves.
;   CMD_GROUP_HITS         the REDS line: which groups the red dots are
;                          touching, with nobody naming a sprite. Bit 1 is
;                          the reds touching each other, which a group does
;                          report about itself.
;   CMD_HIT_IN_GROUP       the RED# line: WHICH red you are touching, 0 for
;                          none. The group said what kind, this says which.

        OUT GPU_COLOR, 0
        OUT GPU_CMD, CMD_CLEAR

; The bulk table needs somewhere to land. One mapping, once.
        OUT GPU_MAP, MAP_GROUPS
        OUT GPU_MAP_HI, gtab >> 8
        OUT GPU_MAP_LO, gtab & 255
        OUT GPU_CMD, CMD_MEMMAP

        OUT GPU_TEXT_COLOR, $FF
        OUT GPU_CMD, CMD_TEXT_STYLE

; ---- the player, sprite 1, group 0 ----
        OUT GPU_SPRITE, 1
        OUT GPU_SRC_BANK, get_bankbyte(pspr)
        OUT GPU_SRC_HI, get_highbyte(pspr)
        OUT GPU_SRC_LO, get_lowbyte(pspr)
        OUT GPU_SPRITE_GROUP, 1        ; bit 0
        OUT GPU_CMD, CMD_SPRITE_DEF
        OUT GPU_SPRITE, 1
        OUT GPU_SPRITE_FRAME, 1        ; the player is always bright
        OUT GPU_CMD, CMD_SPRITE_FRAME
        OUT GPU_SPRITE, 1
        OUT GPU_CMD, CMD_SPRITE_SHOW

; ---- four of each colour ----
        LD A <- 2
        LD [dfirst] <- A
        LD A <- get_bankbyte(rspr)
        LD [dbank] <- A
        LD A <- get_highbyte(rspr)
        LD [dhi] <- A
        LD A <- get_lowbyte(rspr)
        LD [dlo] <- A
        LD A <- 2                      ; bit 1, the reds
        LD [dgrp] <- A
        JSR defrun

        LD A <- 6
        LD [dfirst] <- A
        LD A <- get_bankbyte(gspr)
        LD [dbank] <- A
        LD A <- get_highbyte(gspr)
        LD [dhi] <- A
        LD A <- get_lowbyte(gspr)
        LD [dlo] <- A
        LD A <- 4                      ; bit 2, the greens
        LD [dgrp] <- A
        JSR defrun

        LD A <- 10
        LD [dfirst] <- A
        LD A <- get_bankbyte(bspr)
        LD [dbank] <- A
        LD A <- get_highbyte(bspr)
        LD [dhi] <- A
        LD A <- get_lowbyte(bspr)
        LD [dlo] <- A
        LD A <- 8                      ; bit 3, the blues
        LD [dgrp] <- A
        JSR defrun

; =========================== the frame loop ===========================
loop:   JSR pad
        JSR move
        JSR light
        JSR ask
        JSR hud
        JSR waitframe
        JMP loop

; --- the player, four directions, clamped to the screen.
pad:    IN IO_CONTROLLER -> A
        LD [pd] <- A
        TST A, BTN_LEFT
        JZ pnl
        LD A <- [px]
        JZ pnl                         ; already at the edge
        SUB A <- 2
        LD [px] <- A
pnl:    LD A <- [pd]
        TST A, BTN_RIGHT
        JZ pnr
        LD A <- [px]
        CMP A, 246
        JNC pnr
prok:   LD A <- [px]
        ADD A <- 2
        LD [px] <- A
pnr:    LD A <- [pd]
        TST A, BTN_UP
        JZ pnu
        LD A <- [py]
        JZ pnu
        SUB A <- 2
        LD [py] <- A
pnu:    LD A <- [pd]
        TST A, BTN_DOWN
        JZ pnd
        LD A <- [py]
        CMP A, 246
        JNC pnd
pdok:   LD A <- [py]
        ADD A <- 2
        LD [py] <- A
pnd:    OUT GPU_SPRITE, 1
        OUT GPU_SPRITE_X_HI, 0
        LD A <- [px]
        OUTA GPU_SPRITE_X
        OUT GPU_SPRITE_Y_HI, 0
        LD A <- [py]
        OUTA GPU_SPRITE_Y
        OUT GPU_CMD, CMD_SPRITE_MOVE
        RET

; --- every drifter one step, bouncing off the edges.
; A byte wraps, so walking off either end lands above 248 and one test
; catches both: 2 going left becomes 254, and 248 going right becomes 250.
move:   LD D1 <- drift
        LD A <- 2
        LD [mi] <- A
        LD A <- 12
        LD [mc] <- A
; The ALU reads a byte through a D register and a displacement, so the
; delta at [D1+2] is added where it lies, with no copy to the zero page.
mloop:  LD A <- [D1]
        ADD A <- [D1+2]                ; x + dx, the ALU reading through D1
        CMP A, 249
        JC mxok
        LD A <- 0                      ; off the edge: turn around instead
        SUB A <- [D1+2]
        LD [D1+2] <- A
        JMP mdy
mxok:   LD [D1] <- A                   ; CMP kept the new x in A
mdy:    LD A <- [D1+1]
        ADD A <- [D1+3]
        CMP A, 249
        JC myok
        LD A <- 0
        SUB A <- [D1+3]
        LD [D1+3] <- A
        JMP mput
myok:   LD [D1+1] <- A
mput:   LD A <- [mi]
        OUTA GPU_SPRITE
        OUT GPU_SPRITE_X_HI, 0
        LD A <- [D1]
        OUTA GPU_SPRITE_X
        OUT GPU_SPRITE_Y_HI, 0
        LD A <- [D1+1]
        OUTA GPU_SPRITE_Y
        OUT GPU_CMD, CMD_SPRITE_MOVE
        LD D1 <- D1+4                  ; the next record
        LD A <- [mi]
        ADD A <- 1
        LD [mi] <- A
        LD A <- [mc]
        SUB A <- 1
        LD [mc] <- A
        JNZ mloop
mdone:  RET

; --- CMD_COLLIDE_GROUP_ALL. One command fills a mask for all 256 sprites,
; and the walk below is a load each rather than a command each. A dot with a
; mask of anything at all is touching something, so it shows its bright frame.
light:  OUT GPU_CMD, CMD_COLLIDE_GROUP_ALL
        LD D1 <- gtab+2
        LD A <- 2
        LD [li] <- A
        LD A <- 12
        LD [lc] <- A
lloop:  LD A <- [li]
        OUTA GPU_SPRITE
        LD A <- [D1]
        JZ lset                        ; nothing touched: A is already 0
        LD A <- 1
lset:   OUTA GPU_SPRITE_FRAME
        OUT GPU_CMD, CMD_SPRITE_FRAME
        INC D1
        LD A <- [li]
        ADD A <- 1
        LD [li] <- A
        LD A <- [lc]
        SUB A <- 1
        LD [lc] <- A
        JNZ lloop
ldone:  RET

; --- the three questions the readout answers.
ask:    OUT GPU_SPRITE, 1
        OUT GPU_CMD, CMD_SPRITE_HITS   ; what am I touching, by kind
        IN GPU_HIT -> A
        LD [args] <- A

        OUT GPU_GROUP, 2               ; and the reds, with no sprite named
        OUT GPU_CMD, CMD_GROUP_HITS
        IN GPU_HIT -> A
        LD [args+1] <- A

        OUT GPU_SPRITE, 1              ; and WHICH red, if any
        OUT GPU_GROUP_B, 2
        OUT GPU_CMD, CMD_HIT_IN_GROUP
        IN GPU_HIT -> A
        LD [args+2] <- A
        RET

hud:    OUT GPU_CMD, CMD_TEXT_CLEAR
        OUT GPU_CART_BANK, get_bankbyte(fmt)
        OUT GPU_CART_HI, get_highbyte(fmt)
        OUT GPU_CART_LO, get_lowbyte(fmt)
        OUT GPU_TEXT_ARG_HI, args >> 8
        OUT GPU_TEXT_ARG_LO, args & 255
        OUT GPU_CMD, CMD_PRINTF
        RET

; --- define four sprites in a row, all from one blob and one group.
; DESIGN: every argument is written inside the loop. The data ports clear
; after each command, so an address written once outside it is gone by the
; second pass, and the group byte would clear to 0 and leave three of the
; four in no group at all.
defrun: LD A <- [dfirst]
        LD [dn] <- A
        LD A <- 4
        LD [dc] <- A
drloop: LD A <- [dn]
        OUTA GPU_SPRITE
        LD A <- [dbank]
        OUTA GPU_SRC_BANK
        LD A <- [dhi]
        OUTA GPU_SRC_HI
        LD A <- [dlo]
        OUTA GPU_SRC_LO
        LD A <- [dgrp]
        OUTA GPU_SPRITE_GROUP
        OUT GPU_CMD, CMD_SPRITE_DEF
        LD A <- [dn]
        OUTA GPU_SPRITE
        OUT GPU_CMD, CMD_SPRITE_SHOW
        LD A <- [dn]
        ADD A <- 1
        LD [dn] <- A
        LD A <- [dc]
        SUB A <- 1
        LD [dc] <- A
        JNZ drloop
drdone: RET

waitframe: IN GPU_FRAME -> A
        CMP A, [flast]
        JZ waitframe
        LD [flast] <- A           ; CMP kept the frame in A
        RET

.ram
; The collision table CMD_COLLIDE_GROUP_ALL fills: one mask per sprite, 256
; of them, whether or not the sprite exists.
gtab:   .addr($1000)

; x, y, dx, dy for each drifter. Sprite 2 is the first record.
drift:
        db 30, 40, 2, 1
        db 90, 70, 255, 2
        db 150, 30, 1, 254
        db 210, 90, 254, 255
        db 40, 150, 1, 2
        db 110, 190, 2, 255
        db 170, 140, 254, 1
        db 230, 200, 255, 254
        db 60, 100, 2, 2
        db 130, 120, 255, 1
        db 190, 60, 1, 1
        db 20, 210, 254, 2

px:     db 124
py:     db 124
pd:     db 0
mi:     db 0
mc:     db 0
li:     db 0
lc:     db 0
dfirst: db 0
dbank:  db 0
dhi:    db 0
dlo:    db 0
dgrp:   db 0
dn:     db 0
dc:     db 0
args:   db 0, 0, 0
flast:  db 0

.data
; %b prints the bits themselves, which is the whole point: you steer into a
; red dot and bit 1 of the PLAYER line comes on.
fmt:    db "PLAYER %08hhb\nREDS   %08hhb\nRED#   %hhu", 0

pspr:   db 2, 8, 8
        db $00, $00, $92, $92, $92, $92, $00, $00
        db $00, $92, $92, $92, $92, $92, $92, $00
        db $92, $92, $92, $92, $92, $92, $92, $92
        db $92, $92, $92, $92, $92, $92, $92, $92
        db $92, $92, $92, $92, $92, $92, $92, $92
        db $92, $92, $92, $92, $92, $92, $92, $92
        db $00, $92, $92, $92, $92, $92, $92, $00
        db $00, $00, $92, $92, $92, $92, $00, $00
        db $00, $00, $FF, $FF, $FF, $FF, $00, $00
        db $00, $FF, $FF, $FF, $FF, $FF, $FF, $00
        db $FF, $FF, $FF, $FF, $FF, $FF, $FF, $FF
        db $FF, $FF, $FF, $FF, $FF, $FF, $FF, $FF
        db $FF, $FF, $FF, $FF, $FF, $FF, $FF, $FF
        db $FF, $FF, $FF, $FF, $FF, $FF, $FF, $FF
        db $00, $FF, $FF, $FF, $FF, $FF, $FF, $00
        db $00, $00, $FF, $FF, $FF, $FF, $00, $00
rspr:   db 2, 8, 8
        db $00, $00, $60, $60, $60, $60, $00, $00
        db $00, $60, $60, $60, $60, $60, $60, $00
        db $60, $60, $60, $60, $60, $60, $60, $60
        db $60, $60, $60, $60, $60, $60, $60, $60
        db $60, $60, $60, $60, $60, $60, $60, $60
        db $60, $60, $60, $60, $60, $60, $60, $60
        db $00, $60, $60, $60, $60, $60, $60, $00
        db $00, $00, $60, $60, $60, $60, $00, $00
        db $00, $00, $E0, $E0, $E0, $E0, $00, $00
        db $00, $E0, $E0, $E0, $E0, $E0, $E0, $00
        db $E0, $E0, $E0, $E0, $E0, $E0, $E0, $E0
        db $E0, $E0, $E0, $E0, $E0, $E0, $E0, $E0
        db $E0, $E0, $E0, $E0, $E0, $E0, $E0, $E0
        db $E0, $E0, $E0, $E0, $E0, $E0, $E0, $E0
        db $00, $E0, $E0, $E0, $E0, $E0, $E0, $00
        db $00, $00, $E0, $E0, $E0, $E0, $00, $00
gspr:   db 2, 8, 8
        db $00, $00, $0C, $0C, $0C, $0C, $00, $00
        db $00, $0C, $0C, $0C, $0C, $0C, $0C, $00
        db $0C, $0C, $0C, $0C, $0C, $0C, $0C, $0C
        db $0C, $0C, $0C, $0C, $0C, $0C, $0C, $0C
        db $0C, $0C, $0C, $0C, $0C, $0C, $0C, $0C
        db $0C, $0C, $0C, $0C, $0C, $0C, $0C, $0C
        db $00, $0C, $0C, $0C, $0C, $0C, $0C, $00
        db $00, $00, $0C, $0C, $0C, $0C, $00, $00
        db $00, $00, $1C, $1C, $1C, $1C, $00, $00
        db $00, $1C, $1C, $1C, $1C, $1C, $1C, $00
        db $1C, $1C, $1C, $1C, $1C, $1C, $1C, $1C
        db $1C, $1C, $1C, $1C, $1C, $1C, $1C, $1C
        db $1C, $1C, $1C, $1C, $1C, $1C, $1C, $1C
        db $1C, $1C, $1C, $1C, $1C, $1C, $1C, $1C
        db $00, $1C, $1C, $1C, $1C, $1C, $1C, $00
        db $00, $00, $1C, $1C, $1C, $1C, $00, $00
bspr:   db 2, 8, 8
        db $00, $00, $01, $01, $01, $01, $00, $00
        db $00, $01, $01, $01, $01, $01, $01, $00
        db $01, $01, $01, $01, $01, $01, $01, $01
        db $01, $01, $01, $01, $01, $01, $01, $01
        db $01, $01, $01, $01, $01, $01, $01, $01
        db $01, $01, $01, $01, $01, $01, $01, $01
        db $00, $01, $01, $01, $01, $01, $01, $00
        db $00, $00, $01, $01, $01, $01, $00, $00
        db $00, $00, $03, $03, $03, $03, $00, $00
        db $00, $03, $03, $03, $03, $03, $03, $00
        db $03, $03, $03, $03, $03, $03, $03, $03
        db $03, $03, $03, $03, $03, $03, $03, $03
        db $03, $03, $03, $03, $03, $03, $03, $03
        db $03, $03, $03, $03, $03, $03, $03, $03
        db $00, $03, $03, $03, $03, $03, $03, $00
        db $00, $00, $03, $03, $03, $03, $00, $00
