; A rotating 3D wireframe cube, zooming in and out, forever.
; Eight corners live in .data as (x, y, z) triples around center 128.
; The GPU rotates them with its hardware sine table and projects with
; perspective. The CPU only feeds angles and a scale each device frame.
; Press Pause to freeze it mid spin, Reset to stop.
        OUT GPU_X, 128       ; the pen, which is where a path draws
        OUT GPU_Y, 128
        OUT GPU_CMD, CMD_MOVE_TO
loop:   IN GPU_FRAME
        SUB A <- [fr]
        JZ loop              ; same frame: wait
        IN GPU_FRAME
        LD [fr] <- A
        LD A <- [ay]         ; yaw, 2 steps per frame
        ADD A <- 2
        LD [ay] <- A
        OUTA GPU_ANGLE
        OUT GPU_CMD, CMD_ROT_Y
        LD A <- [ax]         ; pitch, 1 step per frame
        INC A
        LD [ax] <- A
        OUTA GPU_ANGLE
        OUT GPU_CMD, CMD_ROT_X
        LD A <- [si]         ; zoom: scale table in the zero page
        INC A
        AND A <- $1F
        LD [si] <- A
        ADD A <- tab
        LD A <- [A]
        OUTA GPU_SCALE
        OUT GPU_CMD, CMD_SET_SCALE
        OUT GPU_COLOR, 0
        OUT GPU_CMD, CMD_CLEAR
        OUT GPU_COLOR, $FF   ; white edges
        OUT GPU_CMD, CMD_SET_COLOR
        OUT GPU_X, 128       ; the pen again: a command clears the ports
        OUT GPU_Y, 128
        OUT GPU_CMD, CMD_MOVE_TO
        OUT GPU_CART_BANK, get_bankbyte(ring)
        OUT GPU_CART_HI, get_highbyte(ring)
        OUT GPU_CART_LO, get_lowbyte(ring)
        OUT GPU_CMD, CMD_DRAW_PATH3D
        OUT GPU_CART_BANK, get_bankbyte(p15)
        OUT GPU_CART_HI, get_highbyte(p15)
        OUT GPU_CART_LO, get_lowbyte(p15)
        OUT GPU_CMD, CMD_DRAW_PATH3D
        OUT GPU_CART_BANK, get_bankbyte(p26)
        OUT GPU_CART_HI, get_highbyte(p26)
        OUT GPU_CART_LO, get_lowbyte(p26)
        OUT GPU_CMD, CMD_DRAW_PATH3D
        OUT GPU_CART_BANK, get_bankbyte(p37)
        OUT GPU_CART_HI, get_highbyte(p37)
        OUT GPU_CART_LO, get_lowbyte(p37)
        OUT GPU_CMD, CMD_DRAW_PATH3D
        JMP loop

.ram
fr: db 0
ax: db 0
ay: db 0
si: db 0
tab: db 140, 150, 160, 170, 180, 190, 200, 210
     db 220, 230, 240, 250, 255, 250, 240, 230
     db 220, 210, 200, 190, 180, 170, 160, 150
     db 140, 130, 120, 110, 100, 110, 120, 130

.data
; Corner walk 0-1-2-3-0-4-5-6-7-4 draws ten of the twelve edges.
ring: db 10        ; ten points: both squares plus four edges
      db 64, 64, 64, 192, 64, 64, 192, 192, 64, 64, 192, 64, 64, 64, 64
      db 64, 64, 192, 192, 64, 192, 192, 192, 192, 64, 192, 192, 64, 64, 192
p15:  db 2
      db 192, 64, 64, 192, 64, 192
p26:  db 2
      db 192, 192, 64, 192, 192, 192
p37:  db 2
      db 64, 192, 64, 64, 192, 192
