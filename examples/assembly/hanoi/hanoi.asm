; Towers of Hanoi: the machine's stack solving a stack puzzle.
; hanoi(n, from, to, via) calls itself twice. Watch the stack strip
; breathe as the recursion deepens and unwinds. One move per device
; frame - at slow speeds, latch fast-frame (double-click Frame).
;
; Six discs take 63 moves, and then the program HLTs. The HLT pin lights
; in the control strip. A solved puzzle has nothing left to compute, and
; a loop spinning to hold the picture up would only burn the CPU: the
; screen is the GPU's, and it keeps showing it.
        OUT GPU_COLOR, 0
        OUT GPU_CMD, CMD_CLEAR

; three pegs and the base, in gray
        OUT GPU_COLOR, $92
        OUT GPU_CMD, CMD_SET_COLOR
        LD A <- 0
        LD [dp] <- A
poles:  LD D1 <- pegx
        LD A <- [dp]
        LD A <- [D1+A]
        LD [px] <- A
        SUB A <- 2
        OUTA GPU_X
        OUT GPU_Y, 230
        OUT GPU_CMD, CMD_MOVE_TO
        LD A <- [px]
        ADD A <- 2
        OUTA GPU_X
        OUT GPU_Y, 152
        OUT GPU_CMD, CMD_RECT
        LD A <- [dp]
        ADD A <- 1
        LD [dp] <- A
        CMP A, 3
        JNZ poles
base:   OUT GPU_X, 10
        OUT GPU_Y, 232
        OUT GPU_CMD, CMD_MOVE_TO
        OUT GPU_X, 246
        OUT GPU_Y, 237
        OUT GPU_CMD, CMD_RECT

; the six discs, big to small, on peg 0
        LD A <- 6
        LD [ds] <- A
setup:  LD A <- 0
        LD [dp] <- A
        LD A <- 6
        SUB A <- [ds]
        LD [dl] <- A
        LD D1 <- colt
        LD A <- [ds]
        LD A <- [D1+A]
        OUT A -> GPU_COLOR
        OUT GPU_CMD, CMD_SET_COLOR
        JSR ddisc
        LD A <- [ds]
        SUB A <- 1
        LD [ds] <- A
        JNZ setup

; Solve peg 0 -> peg 2, and that is the whole program. 63 moves, which is
; 2^6 - 1 and the fewest there are.
;
; Then HLT. The puzzle is solved, so there is nothing left to compute, and
; the finished picture belongs to the GPU rather than to this loop. A program
; that went round again, or sat polling GPU_FRAME to hold the screen up,
; would run the CPU flat out for the same still image.
solve:  JSR hanoi
        HLT

; hanoi(n, from, to, via): if n = 0 return.
; hanoi(n-1, from, via, to), move disc n, hanoi(n-1, via, to, from).
; Arguments live in the zero page; each level saves them on the stack.
hanoi:  LD A <- [hn]
        JZ hret
        PUSHB A                        ; the test left n in A
        LD A <- [hfrom]
        PUSHB A
        LD A <- [hto]
        PUSHB A
        LD A <- [hvia]
        PUSHB A
        LD A <- [hn]
        SUB A <- 1
        LD [hn] <- A
        LD A <- [hto]
        LD [tmp] <- A
        LD A <- [hvia]
        LD [hto] <- A
        LD A <- [tmp]
        LD [hvia] <- A
        JSR hanoi
        POPB A
        LD [hvia] <- A
        POPB A
        LD [hto] <- A
        POPB A
        LD [hfrom] <- A
        POPB A
        LD [hn] <- A
        JSR move
        LD A <- [hn]
        PUSHB A
        LD A <- [hfrom]
        PUSHB A
        LD A <- [hto]
        PUSHB A
        LD A <- [hvia]
        PUSHB A
        LD A <- [hn]
        SUB A <- 1
        LD [hn] <- A
        LD A <- [hfrom]
        LD [tmp] <- A
        LD A <- [hvia]
        LD [hfrom] <- A
        LD A <- [tmp]
        LD [hvia] <- A
        JSR hanoi
        POPB A
        LD [hvia] <- A
        POPB A
        LD [hto] <- A
        POPB A
        LD [hfrom] <- A
        POPB A
        LD [hn] <- A
hret:   RET

; move disc hn from peg hfrom to peg hto, one frame per move
move:   LD A <- [hfrom]
        JSR seek
        LD A <- [D1]
        SUB A <- 1
        LD [dl] <- A
        LD [D1] <- A
        LD A <- [hfrom]
        LD [dp] <- A
        LD A <- [hn]
        LD [ds] <- A
        LD A <- 0
        OUT A -> GPU_COLOR
        OUT GPU_CMD, CMD_SET_COLOR
        JSR ddisc
        LD A <- $92
        OUT A -> GPU_COLOR
        OUT GPU_CMD, CMD_SET_COLOR
        LD A <- 0
        LD [ds] <- A
        JSR ddisc
        LD A <- [hto]
        JSR seek
        LD A <- [D1]
        LD [dl] <- A
        ADD A <- 1
        LD [D1] <- A
        LD A <- [hto]
        LD [dp] <- A
        LD A <- [hn]
        LD [ds] <- A
        LD D1 <- colt
        LD A <- [hn]
        LD A <- [D1+A]
        OUT A -> GPU_COLOR
        OUT GPU_CMD, CMD_SET_COLOR
        JSR ddisc
        IN GPU_FRAME
        LD [fr] <- A
mwait:  IN GPU_FRAME
        CMP A, [fr]
        JZ mwait
        RET

; D1 := address of cnts[A]. The address adder adds A to the base in one
; step, where walking the pointer took A steps.
seek:   LD D1 <- cnts
        LD D1 <- D1+A
        RET

; draw one rect in the current color. Peg in dp, level in dl, size in ds.
; ds 0 is the 5 pixel pole segment, sizes 1..6 come from the width table.
ddisc:  LD D1 <- pegx
        LD A <- [dp]
        LD A <- [D1+A]
        LD [px] <- A
        LD D1 <- hwt
        LD A <- [ds]
        LD A <- [D1+A]
        LD [hw] <- A
        LD D1 <- yt
        LD A <- [dl]
        LD A <- [D1+A]
        LD [ytop] <- A
        LD A <- [px]
        SUB A <- [hw]
        OUTA GPU_X
        LD A <- [ytop]
        OUTA GPU_Y
        OUT GPU_CMD, CMD_MOVE_TO
        LD A <- [px]
        ADD A <- [hw]
        OUTA GPU_X
        LD A <- [ytop]
        SUB A <- 10
        OUTA GPU_Y
        OUT GPU_CMD, CMD_RECT
        RET

.ram
hn:     db 6
hfrom:  db 0
hto:    db 2
hvia:   db 1
tmp:    db 0
dp:     db 0
dl:     db 0
ds:     db 0
px:     db 0
hw:     db 0
ytop:   db 0
fr:     db 0
cnts:   db 6, 0, 0
pegx:   db 48, 128, 208
hwt:    db 2, 8, 12, 16, 20, 24, 28
yt:     db 220, 208, 196, 184, 172, 160
colt:   db 0, $E0, $F0, $FC, $1C, $03, $8F
