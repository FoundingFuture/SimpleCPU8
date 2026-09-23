; Keyboard playground. The first interactive demo.
; Arrows or WASD move the pen and draw. Hold Z or Ctrl to lift the pen
; and move without drawing. Space clears. Enter recenters the pen.
; All seven buttons pack into one byte read from IO_CONTROLLER.
        OUT GPU_COLOR, 0
        OUT GPU_CMD, CMD_CLEAR
        LD A <- 128
        LD [px] <- A
        LD [py] <- A

loop:   IN A <- IO_CONTROLLER
        LD [inp] <- A

        AND A <- BTN_UP
        JZ noup
        LD A <- [py]
        SUB A <- 1
        LD [py] <- A
noup:   LD A <- [inp]
        AND A <- BTN_DOWN
        JZ nodown
        LD A <- [py]
        ADD A <- 1
        LD [py] <- A
nodown: LD A <- [inp]
        AND A <- BTN_LEFT
        JZ noleft
        LD A <- [px]
        SUB A <- 1
        LD [px] <- A
noleft: LD A <- [inp]
        AND A <- BTN_RIGHT
        JZ noright
        LD A <- [px]
        ADD A <- 1
        LD [px] <- A
noright: LD A <- [inp]
        AND A <- BTN_SPACE
        JZ noclear
        OUT GPU_COLOR, 0
        OUT GPU_CMD, CMD_CLEAR
noclear: LD A <- [inp]
        AND A <- BTN_ENTER
        JZ nocenter
        LD A <- 128
        LD [px] <- A
        LD [py] <- A
nocenter: LD A <- [inp]
        AND A <- BTN_FIRE
        JZ draw
        JMP wait
draw:   LD A <- [px]
        OUTA GPU_X
        LD A <- [py]
        OUTA GPU_Y
        OUT GPU_PIXEL, $FC
        OUT GPU_CMD, CMD_PLOT

wait:   IN A <- GPU_FRAME
        LD [fr] <- A
poll:   IN A <- GPU_FRAME
        SUB A <- [fr]
        JZ poll
        JMP loop

.ram
px:     db 128
py:     db 128
inp:    db 0
fr:     db 0
