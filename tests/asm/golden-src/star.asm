; A vector star from the cartridge, spinning and breathing forever.
; The star is 11 normalized (x, y) byte pairs in .data. Each device frame
; the CPU bumps the z rotation, looks the scale up in a zero page table,
; clears, and asks the GPU to redraw the path. Pause or Reset ends it.
        OUT GPU_X, 128       ; the pen, which is where the path draws
        OUT GPU_Y, 128
        OUT GPU_CMD, CMD_MOVE_TO
loop:   IN GPU_FRAME
        SUB A <- [fr]
        JZ loop              ; same frame: wait
        IN GPU_FRAME
        LD [fr] <- A
        LD A <- [az]         ; spin
        ADD A <- 3
        LD [az] <- A
        OUTA GPU_ANGLE
        OUT GPU_CMD, CMD_ROT_Z
        LD A <- [si]         ; breathe: scale from a table
        INC A
        AND A <- $1F
        LD [si] <- A
        ADD A <- tab
        LD A <- [A]          ; zero page table lookup through A
        OUTA GPU_SCALE
        OUT GPU_CMD, CMD_SET_SCALE
        OUT GPU_COLOR, 0
        OUT GPU_CMD, CMD_CLEAR
        OUT GPU_COLOR, $1F   ; cyan
        OUT GPU_CMD, CMD_SET_COLOR
        OUT GPU_X, 128       ; CLEAR does not move the pen, but SET_COLOR
        OUT GPU_Y, 128       ; cleared the ports, so say it again
        OUT GPU_CMD, CMD_MOVE_TO
        OUT GPU_CART_BANK, get_bankbyte(star)
        OUT GPU_CART_HI, get_highbyte(star)
        OUT GPU_CART_LO, get_lowbyte(star)
        OUT GPU_CMD, CMD_DRAW_PATH   ; the blob leads with its point count
        JMP loop

.ram
fr: db 0
az: db 0
si: db 0
tab: db 120, 132, 144, 156, 168, 180, 192, 204
     db 216, 228, 240, 252, 255, 252, 240, 228
     db 216, 204, 192, 180, 168, 156, 144, 132
     db 120, 108, 96, 84, 72, 84, 96, 108

.data
star: db 11        ; the blob leads with its point count
      db 248, 128, 167, 156, 165, 242, 113, 174, 31, 199
      db 80, 128, 31, 57, 113, 82, 165, 14, 167, 100, 248, 128
