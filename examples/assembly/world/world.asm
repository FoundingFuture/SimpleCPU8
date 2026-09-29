; A pyramid spinning in a 3D world the GPU draws by itself.
;
; MODE_WORLD is the GPU holding a world and rendering it from a camera, every
; frame, with no drawing commands from the CPU at all. Look at the main loop
; below: it adds a number to two bytes of RAM and waits. That is the whole
; per-frame cost of an animated 3D scene.
;
; The scene lives in the CPU's own data RAM, at a base the program nominates.
; So changing the world is writing bytes. No command, no port, no upload.
;
;   the camera   32 bytes at the base
;   object N     16 bytes at base + 32 + N * 16
;
; Sixteen bytes is not a taste. The CPU has no multiply, so N * 16 has to be
; four doublings, and a 24 byte record would need a real multiply for every
; object the program touches.
;
; The palette layout is one sentence: the high nibble is the colour and the
; low nibble is the distance. Sixteen ramps of sixteen shades fills all 256
; entries, so an object is one colour and how far away a piece of it is picks
; the shade.

        OUT GPU_COLOR, 0
        OUT GPU_CMD, CMD_CLEAR

; ---- the sky ----
;
; Palette entry 0 is the background, and the world's ramps fade toward it.
; So set it FIRST. A dusk blue here, rather than black, and the pyramids
; then fade into the sky as they recede instead of going dark against it.
; Set it to 0,0,0 and this is a black space scene again.
;
; Entry 0 is ramp 0's shade 0, so CMD_WORLD_RAMP sets it. Nothing here uses
; ramp 0 for an object, so the fifteen shades it also fills go unread.
        OUT GPU_RAMP, 0
        OUT GPU_RED, 24
        OUT GPU_GREEN, 32
        OUT GPU_BLUE, 56
        OUT GPU_CMD, CMD_WORLD_RAMP

; ---- one ramp, sixteen shades ----
;
; Shade 0 is written with the ordinary palette ports, then CMD_WORLD_RAMP
; fans it out to the other fifteen, fading toward the sky. That is one
; command instead of sixteen palette entries typed by hand.
        OUT GPU_RAMP, 1
        OUT GPU_RED, 96
        OUT GPU_GREEN, 255
        OUT GPU_BLUE, 160
        OUT GPU_CMD, CMD_WORLD_RAMP

        OUT GPU_RAMP, 2
        OUT GPU_RED, 255
        OUT GPU_GREEN, 160
        OUT GPU_BLUE, 64
        OUT GPU_CMD, CMD_WORLD_RAMP

; ---- the mesh ----
;
; The blob leads with its counts: five vertices and eight edges, both 16 bit.
; The command that loads it takes only a pointer, so a mesh that grows needs
; no change here. Faces are not stored, edges are, so the four triangular
; sides are their eight edges with the shared ones kept once.
        OUT GPU_MESH, 0                ; defining mesh 0
        OUT GPU_SRC_BANK, get_bankbyte(pyramid)
        OUT GPU_SRC_HI, get_highbyte(pyramid)
        OUT GPU_SRC_LO, get_lowbyte(pyramid)
        OUT GPU_CMD, CMD_MESH_LOAD     ; the blob carries its own counts

; ---- the scene ----

        OUT GPU_ADDR_HI, scene >> 8
        OUT GPU_ADDR_LO, scene & 255
        OUT GPU_COUNT_HI, 0
        OUT GPU_COUNT_LO, 2            ; two object slots
        OUT GPU_CMD, CMD_SET_WORLDMODE ; the mode and the scene, together

; ---- spin ----
;
; Two bytes a frame. The GPU transforms, clips, projects, shades and draws
; the whole world from that. A yaw is a 16 bit word, and a D register adds
; to a whole word at once: load it, step it through the address adder,
; store it back.
loop:   LD D1 <- [spin]                ; the yaw of object 0
        LD D1 <- D1+160
        LD [spin] <- D1

; The second pyramid turns the other way, so the pair is obviously two
; objects rather than one drawn twice.
        LD D1 <- [spin2]
        LD D1 <- D1-96
        LD [spin2] <- D1

        JSR waitframe
        JMP loop

; The device clock. GPU_FRAME counts cycles, so a program waits on it rather
; than counting instructions, and the animation runs at the same speed
; whatever the CPU speed is.
waitframe: IN GPU_FRAME -> A
        CMP A, [flast]
        JZ waitframe
        LD [flast] <- A           ; CMP kept the frame in A
        RET

.ram
; The scene: a camera, then the objects. Everything the GPU draws is here,
; so every change to the world is an ordinary store.
scene:
; The camera, 32 bytes.
; Back along +Z and a little above the ground, so the pyramid sits in the
; middle of the screen rather than filling it. The camera looks down -Z with
; no pitch, so raising it moves the picture down.
cam:    dw 0, 200, 900                 ; position
        dw 0, 0, 0                     ; yaw, pitch, roll
        dw 1, 2000                     ; near and far planes
        dw 256                         ; focal length, 256 is 1.0
        dw 0, 0, 0, 0, 0, 0, 0         ; the sun's bytes, reserved and zero

; Object 0: the spinning pyramid, ramp 1, at the origin.
obj0:   db 1                           ; flags, bit 0 is active
        db 0                           ; mesh 0
        db 1                           ; ramp 1
        db 64                          ; scale, 64 is 1.0
        dw 0, 0, 0                     ; position
spin:   dw 0                           ; yaw, the two bytes the loop writes
        dw 0, 0                        ; pitch and roll

; Object 1: a second pyramid, further away and turning the other way.
obj1:   db 1
        db 0
        db 2                           ; ramp 2
        db 40                          ; smaller
        dw 400, 0, 64960               ; to the right and further off
spin2:  dw 0
        dw 0, 0

flast:  db 0

.data
; Five vertices then eight edges, in the formats a GPU takes natively: three
; signed 16 bit for a position, two unsigned 16 bit for an edge.
pyramid:
        dw 5, 8                        ; five vertices, eight edges
        dw 65136, 0, 65136             ; -400, 0, -400
        dw 400, 0, 65136
        dw 400, 0, 400
        dw 65136, 0, 400
        dw 0, 500, 0                   ; the apex
        dw 0, 1                        ; the base, four edges
        dw 1, 2
        dw 2, 3
        dw 3, 0
        dw 0, 4                        ; the four sides
        dw 1, 4
        dw 2, 4
        dw 3, 4
