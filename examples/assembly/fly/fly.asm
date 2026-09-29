; Fly through a world the GPU draws by itself.
;
; Arrow keys turn and move. Left and right yaw the camera, up and down drive
; it forwards and back. Z rises, X sinks.
;
; Nothing in this program draws. The whole per-frame cost is reading the
; controller and writing a few bytes of the camera record, which lives in the
; CPU's own data RAM. The GPU reads that record every frame and renders the
; world from it.
;
; The world is eight pyramids at different distances, in four colour ramps.
; They share ONE mesh: the geometry is stored once and each object is a
; placement of it. That is the whole reason meshes and objects are separate.
;
; Watch what distance does to the colour. The high nibble is the ramp and the
; low nibble is the shade, so a pyramid far away is drawn in the same colour
; as a near one, in a darker shade of it.

        OUT GPU_COLOR, 0
        OUT GPU_CMD, CMD_CLEAR

; ---- four ramps ----
;
; Shade 0 goes in with the ordinary palette ports, then CMD_WORLD_RAMP fans
; it out to sixteen shades fading toward black.
        LD D1 <- &ramps
        LD A <- 0
        LD [ri] <- A
; One command a ramp. It takes the colour directly, so there is no palette
; index to compute: the ramp number IS the argument.
rloop:  LD A <- [ri]
        ADD A <- 1                     ; ramps 1 to 4
        OUTA GPU_RAMP
        LD A <- [D1]+
        OUTA GPU_RED
        LD A <- [D1]+
        OUTA GPU_GREEN
        LD A <- [D1]+
        OUTA GPU_BLUE
        OUT GPU_CMD, CMD_WORLD_RAMP
        LD A <- [ri]
        ADD A <- 1
        LD [ri] <- A
        CMP A, 4
        JNZ rloop
rdone:

; ---- one mesh, eight placements ----

        OUT GPU_MESH, 0
        OUT GPU_SRC_BANK, get_bankbyte(pyramid)
        OUT GPU_SRC_HI, get_highbyte(pyramid)
        OUT GPU_SRC_LO, get_lowbyte(pyramid)
        OUT GPU_CMD, CMD_MESH_LOAD

        OUT GPU_ADDR_HI, scene >> 8
        OUT GPU_ADDR_LO, scene & 255
        OUT GPU_COUNT_HI, 0
        OUT GPU_COUNT_LO, 8
        OUT GPU_CMD, CMD_SET_WORLDMODE

; ---- the readout ----
;
; The printf overlay is a plane of characters the GPU draws over whatever is
; on screen, and it works in this mode as it does over graphics.
;
; Every palette entry in this mode is a ramp and a shade, so give the text an
; entry in a ramp no object uses. Text is not shaded by distance, so sharing
; an entry with geometry makes the two impossible to tell apart.
        OUT GPU_RAMP, 15               ; ramp 15's shade 0 is entry 240
        OUT GPU_RED, 255
        OUT GPU_GREEN, 255
        OUT GPU_BLUE, 255
        OUT GPU_CMD, CMD_WORLD_RAMP
        OUT GPU_TEXT_COLOR, 240
        OUT GPU_CMD, CMD_TEXT_STYLE

; ---- the flight ----

loop:   IN IO_CONTROLLER -> A
        LD [pad] <- A

        TST A, BTN_LEFT
        JZ nleft
        JSR yawleft
nleft:  LD A <- [pad]
        TST A, BTN_RIGHT
        JZ nright
        JSR yawright
nright: LD A <- [pad]
        TST A, BTN_UP
        JZ nup
        JSR forward
nup:    LD A <- [pad]
        TST A, BTN_DOWN
        JZ ndown
        JSR backward
ndown:  LD A <- [pad]
        TST A, BTN_FIRE
        JZ nfire
        JSR riseup
nfire:
        JSR hud
        JSR waitframe
        JMP loop

; One printf a frame. Clear the plane, push the two values high byte first,
; point the cart latches at the template, run the command. The template is
; ROM, which is where a template belongs. The ramp colours a few lines down
; are in RAM, because the CPU has to read those itself.
; The arguments are bytes in RAM and the command takes a pointer, so their
; number is not bounded by how many OUTs this is willing to write. They are
; copied into one small block because the template wants yaw then z, and the
; camera record keeps pitch and roll between them.
hud:    LD A <- [cyaw]
        LD [args] <- A
        LD A <- [cyaw+1]
        LD [args+1] <- A
        LD A <- [cz]
        LD [args+2] <- A
        LD A <- [cz+1]
        LD [args+3] <- A
        OUT GPU_CMD, CMD_TEXT_CLEAR
        OUT GPU_CART_BANK, get_bankbyte(hudfmt)
        OUT GPU_CART_HI, get_highbyte(hudfmt)
        OUT GPU_CART_LO, get_lowbyte(hudfmt)
        OUT GPU_TEXT_ARG_HI, args >> 8
        OUT GPU_TEXT_ARG_LO, args & 255
        OUT GPU_CMD, CMD_PRINTF
        RET

; Turning is one 16-bit add to the camera's yaw. It wraps at a full turn on
; its own, because the field is exactly 16 bits wide. A D register adds a
; whole word through the address adder in one instruction, so the word goes
; into D2, takes the step and goes back. The step is 400, and D2-400 is the
; turn the other way.
yawleft: LD D2 <- [cyaw]
        LD D2 <- D2-400
        LD [cyaw] <- D2
        RET

yawright: LD D2 <- [cyaw]
        LD D2 <- D2+400
        LD [cyaw] <- D2
        RET

; Moving straight along -Z is enough to show the world go by, and it keeps
; this demo about the renderer rather than about trigonometry on the CPU.
forward: LD D2 <- [cz]
        LD D2 <- D2-24
        LD [cz] <- D2
        RET

backward: LD D2 <- [cz]
        LD D2 <- D2+24
        LD [cz] <- D2
        RET

riseup: LD D2 <- [cy]
        LD D2 <- D2+8
        LD [cy] <- D2
        RET

waitframe: IN GPU_FRAME -> A
        CMP A, [flast]
        JZ waitframe
        LD [flast] <- A           ; CMP kept the frame in A
        RET

.ram
scene:
cx:     dw 0
cy:     dw 200
cz:     dw 1200
cyaw:   dw 0
cpitch: dw 0
croll:  dw 0
; The far plane clears the furthest pyramid on purpose. Set it to 4000 and
; everything past 4000 clamps to shade 15 together, so the back half of the
; world stops being sorted by distance and reads as one flat wall.
        dw 1, 10000                    ; near and far
        dw 256                         ; focal length, 256 is 90 degrees
        dw 0, 0, 0, 0, 0, 0, 0         ; the sun's bytes, reserved

; Eight placements of ONE mesh. Each is sixteen bytes, so object N sits at
; scene + 32 + N * 16 and the CPU reaches it with four doublings.
;
; The four bytes are: active, mesh, ramp, scale. Then position, then the
; three angles. Every one of these names mesh 0, so the pyramid's five
; vertices and eight edges are stored once and drawn eight times.
objs:
        db 1, 0, 1, 64                 ; near, green, full size
        dw 0, 0, 0
        dw 0, 0, 0
        db 1, 0, 2, 48
        dw 900, 0, 64536
        dw 0, 0, 0
        db 1, 0, 3, 56
        dw 64636, 0, 63536
        dw 0, 0, 0
        db 1, 0, 4, 72
        dw 1400, 0, 62536
        dw 0, 0, 0
        db 1, 0, 1, 40
        dw 63736, 0, 61536
        dw 0, 0, 0
        db 1, 0, 2, 64
        dw 500, 0, 60536
        dw 0, 0, 0
        db 1, 0, 3, 88
        dw 64036, 0, 59036
        dw 0, 0, 0
        db 1, 0, 4, 48
        dw 1800, 0, 58036
        dw 0, 0, 0

; The four ramp colours the loop above reads. These live in .ram, not in
; .data, because the CPU cannot read the cartridge. Only the GPU and the
; audio chip can. A table the CPU walks with a pointer has to be in RAM.
ramps:  db 96,255,160                  ; ramp 1, green
        db 255,160,64                  ; ramp 2, orange
        db 128,176,255                 ; ramp 3, blue
        db 255,224,96                  ; ramp 4, yellow

pad:    db 0
ri:     db 0
args:   db 0, 0, 0, 0
flast:  db 0

.data
hudfmt: db "YAW %u  Z %d", 0

pyramid:
        dw 5, 8                        ; five vertices, eight edges
        dw 65136, 0, 65136
        dw 400, 0, 65136
        dw 400, 0, 400
        dw 65136, 0, 400
        dw 0, 500, 0
        dw 0, 1
        dw 1, 2
        dw 2, 3
        dw 3, 0
        dw 0, 4
        dw 1, 4
        dw 2, 4
        dw 3, 4
