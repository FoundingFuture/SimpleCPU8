; Concentric rings, then palette rotation does the animating.
; The CPU draws once and just watches the frame counter afterwards:
; the color cycling is pure device time. Run this at fast speed.
        OUT GPU_X, 128
        OUT GPU_Y, 128
        OUT GPU_CMD, CMD_MOVE_TO
        LD A <- 180
        LD [r] <- A          ; radius: 180 down to 6
        LD A <- 1
        LD [c] <- A          ; color walks the palette
rings:  LD A <- [c]
        OUTA GPU_COLOR
        OUT GPU_CMD, CMD_SET_COLOR
        LD A <- [r]
        OUTA GPU_RADIUS
        OUT GPU_CMD, CMD_CIRCLE
        LD A <- [c]
        ADD A <- 7
        LD [c] <- A
        LD A <- [r]
        SUB A <- 6
        LD [r] <- A
        JZ spin
        JMP rings

; Rotate palette entries 1..255, one step per frame. Entry 0 stays:
; the background keeps its color while the rings cycle.
spin:   OUT GPU_FIRST, 1
        OUT GPU_LAST, 255
        OUT GPU_CMD, CMD_ROTATE_RANGE
        OUT GPU_SPEED, 1
        OUT GPU_CMD, CMD_ROTATE_SPEED

; The CPU is done. The rotation is device time: it spins as long as
; the cycles keep coming. Press Pause to freeze it, Reset to stop.
wait:   JMP wait

.ram
r: db 0
c: db 0
