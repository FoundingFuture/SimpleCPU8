; Keyboard playground. The first interactive demo.
; Arrows or WASD move the pen and draw. Hold Z or Ctrl to lift the pen
; and move without drawing. Space clears. Enter recenters the pen.
; All seven buttons pack into one byte read from IO_CONTROLLER. TST tests
; one button's bit and leaves the byte in A.
        OUT GPU_COLOR, 0
        OUT GPU_CMD, CMD_CLEAR
        LD A <- 128
        LD [px] <- A
        LD [py] <- A

loop:   IN A <- IO_CONTROLLER
        LD [inp] <- A

        TST A, BTN_UP
        JZ noup
        LD A <- [py]
        DEC A
        LD [py] <- A
noup:   LD A <- [inp]
        TST A, BTN_DOWN
        JZ nodown
        LD A <- [py]
        INC A
        LD [py] <- A
nodown: LD A <- [inp]
        TST A, BTN_LEFT
        JZ noleft
        LD A <- [px]
        DEC A
        LD [px] <- A
noleft: LD A <- [inp]
        TST A, BTN_RIGHT
        JZ noright
        LD A <- [px]
        INC A
        LD [px] <- A
noright: LD A <- [inp]
        TST A, BTN_SPACE
        JZ noclear
        OUT GPU_COLOR, 0
        OUT GPU_CMD, CMD_CLEAR
noclear: LD A <- [inp]
        TST A, BTN_ENTER
        JZ nocenter
        LD A <- 128
        LD [px] <- A
        LD [py] <- A
nocenter: LD A <- [inp]
        TST A, BTN_FIRE
        JNZ wait
draw:   LD A <- [px]
        OUTA GPU_X
        LD A <- [py]
        OUTA GPU_Y
        OUT GPU_PIXEL, $FC
        OUT GPU_CMD, CMD_PLOT

wait:   IN A <- GPU_FRAME
        LD [fr] <- A
poll:   IN A <- GPU_FRAME
        CMP A, [fr]
        JZ poll
        JMP loop

.ram
px:     db 128
py:     db 128
inp:    db 0
fr:     db 0
