; Two coprocessors and a CPU that cannot multiply.
;
; The pyramid below is placed by a 4x4 matrix of 64 bit floats. Every frame
; that matrix is multiplied by a rotation, so the object orbits the origin.
;
; The CPU does none of that arithmetic. It CANNOT: it has no multiply
; instruction at all, and it cannot represent a 64 bit float. What it does
; each frame is write one command byte and copy 128 bytes. The ACP does the
; matrix product. The GPU reads the result and draws the world from it.
;
; The three memories in play:
;
;   the ACP block   384 bytes of data RAM: A, then B, then the result
;   the matrix table  the GPU reads matrices from data RAM, by id
;   the scene         the camera and the objects, also in data RAM
;
; The ACP block and the matrix table overlap ON PURPOSE. The GPU's matrix 0
; IS the ACP's result slot. Nothing copies the matrix to the GPU, because
; there is nowhere to copy it to.
;
; Press fire to swap the camera's projection. The camera takes matrices too,
; a view and a projection, the way GL_MODELVIEW and GL_PROJECTION were always
; two separate things. One byte of the camera record picks between the
; perspective the GPU builds from a focal length and the orthographic matrix
; stored below. Watch the pyramid stop growing as it swings toward you: no
; focal length can say that.

        OUT GPU_COLOR, 0
        OUT GPU_CMD, CMD_CLEAR

; A near-black blue, so the ramps fade into something rather than nothing.
; Entry 0 is ramp 0's shade 0, so the same command sets the background.
        OUT GPU_RAMP, 0
        OUT GPU_RED, 12
        OUT GPU_GREEN, 12
        OUT GPU_BLUE, 28
        OUT GPU_CMD, CMD_WORLD_RAMP

        OUT GPU_RAMP, 1
        OUT GPU_RED, 255
        OUT GPU_GREEN, 200
        OUT GPU_BLUE, 64
        OUT GPU_CMD, CMD_WORLD_RAMP

        OUT GPU_RAMP, 2
        OUT GPU_RED, 96
        OUT GPU_GREEN, 255
        OUT GPU_BLUE, 200
        OUT GPU_CMD, CMD_WORLD_RAMP

; ---- the mesh ----

        OUT GPU_MESH, 0
        OUT GPU_SRC_BANK, get_bankbyte(pyramid)
        OUT GPU_SRC_HI, get_highbyte(pyramid)
        OUT GPU_SRC_LO, get_lowbyte(pyramid)
        OUT GPU_CMD, CMD_MESH_LOAD

; ---- the scene, and the matrix table ----

        OUT GPU_ADDR_HI, scene >> 8
        OUT GPU_ADDR_LO, scene & 255
        OUT GPU_COUNT_HI, 0
        OUT GPU_COUNT_LO, 2
        OUT GPU_CMD, CMD_SET_WORLDMODE

; Matrix 0 is the ACP's result slot. The GPU reads 128 bytes of big-endian
; float64 from there, row major, which is exactly what the ACP leaves.
; Matrix 1 sits right behind it and is the orthographic projection below.
        OUT GPU_ADDR_HI, result >> 8
        OUT GPU_ADDR_LO, result & 255
        OUT GPU_COUNT_HI, 0
        OUT GPU_COUNT_LO, 2
        OUT GPU_CMD, CMD_MATRIX_MAP

; The camera's projection id, written once. The FLAG is what gets toggled.
; These bytes sit inside the scene record, past the zero page, so they are
; reached with a pointer rather than by direct address.
        LD D1 <- &cflags
        LD A <- 0
        LD [D1] <- A                   ; flags, off to begin with
        LD D1 <- &projhi
        LD [D1]+ <- A                  ; projection id, high byte
        LD A <- 1
        LD [D1] <- A                   ; low byte, so the id is matrix 1

; The readout gets a palette entry of its own, in a ramp no object uses.
; Text is not shaded by distance, so sharing an entry with geometry would
; make the two impossible to tell apart, on screen and in a test.
        OUT GPU_RAMP, 15               ; ramp 15's shade 0 is entry 240
        OUT GPU_RED, 255
        OUT GPU_GREEN, 255
        OUT GPU_BLUE, 255
        OUT GPU_CMD, CMD_WORLD_RAMP
        OUT GPU_TEXT_COLOR, 240
        OUT GPU_CMD, CMD_TEXT_STYLE

; ---- the ACP, set up once ----
;
; A is 4 by 4, B is 4 by 4, so the product is 4 by 4. The shape ports say
; that once and never change. ACP_F64 sets the element type AND the result
; type together.

        OUT ACP_ADDR_HI, rot >> 8
        OUT ACP_ADDR_LO, rot & 255
        OUT ACP_FMT, ACP_F64
        OUT ACP_ROWS, 4
        OUT ACP_COLS, 4
        OUT ACP_COLS_B, 4

; ---- one frame ----
;
; The whole per-frame cost: one command byte to the ACP, then 128 bytes
; copied back so the next product continues from this one.
loop:   OUT ACP_CMD, ACP_MATMUL

        JSR toggle

; result becomes the next frame's B. The product is R times M, in that
; order, so the rotation is applied in world space and the pyramid goes
; round the origin rather than spinning where it stands.
        LD D1 <- &result
        LD D2 <- &orbit
        LD A <- 128
        LD [n] <- A
copy:   LD A <- [D1]+
        LD [D2]+ <- A
        LD A <- [n]
        SUB A <- 1
        LD [n] <- A
        JNZ copy
done:

        JSR hud
        JSR waitframe
        JMP loop

; Press fire to swap the projection. One byte of the camera record decides
; whether the GPU builds a perspective from the focal length or reads the
; orthographic matrix. Under orthographic the far pyramid stops shrinking,
; which no focal length can express.
toggle: IN IO_CONTROLLER -> A
        AND A <- BTN_FIRE
        LD [now] <- A
        JZ store
        LD A <- [was]
        TST A, BTN_FIRE
        JNZ store                      ; held since last frame, do nothing
flip:   LD D1 <- &cflags
        LD A <- [D1]
        XOR A <- CAM_FLAG_PROJ_MATRIX
        LD [D1] <- A
store:  LD A <- [now]
        LD [was] <- A
        RET

hud:    OUT GPU_CMD, CMD_TEXT_CLEAR
        OUT GPU_CART_BANK, get_bankbyte(msg)
        OUT GPU_CART_HI, get_highbyte(msg)
        OUT GPU_CART_LO, get_lowbyte(msg)
        OUT GPU_CMD, CMD_PRINTF        ; no conversions, so no arguments
        LD D1 <- &cflags
        LD A <- [D1]
        TST A, CAM_FLAG_PROJ_MATRIX
        JZ persp
        OUT GPU_CART_BANK, get_bankbyte(orthomsg)
        OUT GPU_CART_HI, get_highbyte(orthomsg)
        OUT GPU_CART_LO, get_lowbyte(orthomsg)
        OUT GPU_CMD, CMD_PRINTF
        RET
persp:  OUT GPU_CART_BANK, get_bankbyte(perspmsg)
        OUT GPU_CART_HI, get_highbyte(perspmsg)
        OUT GPU_CART_LO, get_lowbyte(perspmsg)
        OUT GPU_CMD, CMD_PRINTF
        RET

waitframe: IN GPU_FRAME -> A
        CMP A, [flast]
        JZ waitframe
        LD [flast] <- A           ; CMP kept the frame in A
        RET

.ram
; The scratch bytes go first, so they land in the zero page. Direct byte
; addressing reaches the first 256 bytes only, and the matrices below are
; 384 bytes on their own.
n:      db 0
now:    db 0
was:    db 0
flast:  db 0

; The ACP's block, in the order the device reads it: A, then B, then the
; result. Every value is a big-endian 64 bit float, written here as the
; eight bytes it is. The CPU cannot build one of these, so it does not try.

; A: a rotation about Y of one 256th of a turn. Written once, never touched.
rot:
        db $3F,$EF,$FD,$88,$60,$84,$CD,$0D   ; cos t
        db $00,$00,$00,$00,$00,$00,$00,$00
        db $3F,$99,$21,$55,$F7,$A3,$66,$7E   ; sin t
        db $00,$00,$00,$00,$00,$00,$00,$00
        db $00,$00,$00,$00,$00,$00,$00,$00
        db $3F,$F0,$00,$00,$00,$00,$00,$00   ; 1
        db $00,$00,$00,$00,$00,$00,$00,$00
        db $00,$00,$00,$00,$00,$00,$00,$00
        db $BF,$99,$21,$55,$F7,$A3,$66,$7E   ; -sin t
        db $00,$00,$00,$00,$00,$00,$00,$00
        db $3F,$EF,$FD,$88,$60,$84,$CD,$0D   ; cos t
        db $00,$00,$00,$00,$00,$00,$00,$00
        db $00,$00,$00,$00,$00,$00,$00,$00
        db $00,$00,$00,$00,$00,$00,$00,$00
        db $00,$00,$00,$00,$00,$00,$00,$00
        db $3F,$F0,$00,$00,$00,$00,$00,$00   ; 1

; B: where the pyramid starts. A translation 400 out along x. After the
; first product this holds the accumulated orbit.
orbit:
        db $3F,$F0,$00,$00,$00,$00,$00,$00   ; 1
        db $00,$00,$00,$00,$00,$00,$00,$00
        db $00,$00,$00,$00,$00,$00,$00,$00
        db $40,$79,$00,$00,$00,$00,$00,$00   ; 400
        db $00,$00,$00,$00,$00,$00,$00,$00
        db $3F,$F0,$00,$00,$00,$00,$00,$00   ; 1
        db $00,$00,$00,$00,$00,$00,$00,$00
        db $00,$00,$00,$00,$00,$00,$00,$00
        db $00,$00,$00,$00,$00,$00,$00,$00
        db $00,$00,$00,$00,$00,$00,$00,$00
        db $3F,$F0,$00,$00,$00,$00,$00,$00   ; 1
        db $00,$00,$00,$00,$00,$00,$00,$00
        db $00,$00,$00,$00,$00,$00,$00,$00
        db $00,$00,$00,$00,$00,$00,$00,$00
        db $00,$00,$00,$00,$00,$00,$00,$00
        db $3F,$F0,$00,$00,$00,$00,$00,$00   ; 1

; The result. The ACP writes it and the GPU reads it. Nothing else touches
; it, and nothing copies it anywhere the GPU can see, because it is already
; where the GPU looks.
result:
        db 0,0,0,0,0,0,0,0
        db 0,0,0,0,0,0,0,0
        db 0,0,0,0,0,0,0,0
        db 0,0,0,0,0,0,0,0
        db 0,0,0,0,0,0,0,0
        db 0,0,0,0,0,0,0,0
        db 0,0,0,0,0,0,0,0
        db 0,0,0,0,0,0,0,0
        db 0,0,0,0,0,0,0,0
        db 0,0,0,0,0,0,0,0
        db 0,0,0,0,0,0,0,0
        db 0,0,0,0,0,0,0,0
        db 0,0,0,0,0,0,0,0
        db 0,0,0,0,0,0,0,0
        db 0,0,0,0,0,0,0,0
        db 0,0,0,0,0,0,0,0

; Matrix 1: an orthographic projection, the thing a focal length cannot say.
; x and y scale by 1/800 so the world's 800 units fill half the screen, and z
; maps the near and far planes onto -1 and 1 with no divide at all. The GPU
; reads it exactly as it reads the ACP's output: nothing here is special.
ortho:
        db $3F,$54,$7A,$E1,$47,$AE,$14,$7B   ; 1/800
        db $00,$00,$00,$00,$00,$00,$00,$00
        db $00,$00,$00,$00,$00,$00,$00,$00
        db $00,$00,$00,$00,$00,$00,$00,$00
        db $00,$00,$00,$00,$00,$00,$00,$00
        db $3F,$54,$7A,$E1,$47,$AE,$14,$7B   ; 1/800
        db $00,$00,$00,$00,$00,$00,$00,$00
        db $00,$00,$00,$00,$00,$00,$00,$00
        db $00,$00,$00,$00,$00,$00,$00,$00
        db $00,$00,$00,$00,$00,$00,$00,$00
        db $BF,$40,$63,$5A,$53,$9A,$DA,$CF   ; -2 / (far - near)
        db $BF,$F0,$02,$0C,$6B,$4A,$73,$5B   ; -(far + near) / (far - near)
        db $00,$00,$00,$00,$00,$00,$00,$00
        db $00,$00,$00,$00,$00,$00,$00,$00
        db $00,$00,$00,$00,$00,$00,$00,$00   ; w stays 1, which is what makes
        db $3F,$F0,$00,$00,$00,$00,$00,$00   ; it orthographic

scene:
        dw 0, 250, 1400                ; camera position
        dw 0, 0, 0                     ; yaw, pitch, roll
        dw 1, 4000                     ; near and far, matching ortho above
        dw 256                         ; focal length
; The tail of the camera record, which used to be all reserved. Offsets are
; from the start of the record: 18 to 23 the sun, 24 the ambient floor, 25
; the flags, 26 and 27 the view matrix id, 28 and 29 the projection's.
        dw 0, 0, 0                     ; 18 to 23, the sun's direction
        db 0                           ; 24, ambient floor
cflags: db 0                           ; 25, bit 1 is what fire toggles
        dw 0                           ; 26 and 27, view id, unused here
projhi: db 0                           ; 28
projlo: db 0                           ; 29, together the id 1
        dw 0                           ; 30 and 31, still reserved

; Object 0 takes its transform from a matrix. Flag bit 1 says so, and then
; bytes 4 and 5 are the matrix id rather than a position.
        db OBJ_FLAG_ACTIVE | OBJ_FLAG_MATRIX, 0, 1, 0
        dw 0                           ; matrix id 0
        dw 0, 0                        ; unused in this mode
        dw 0, 0, 0

; Object 1 is an ordinary record, sitting still at the centre. The two ways
; of placing an object are side by side in one scene on purpose.
        db OBJ_FLAG_ACTIVE, 0, 2, 48
        dw 0, 0, 0
        dw 0, 0, 0

.data
msg:    db "ACP composes, GPU draws\n", 0
perspmsg: db "fire: perspective", 0
orthomsg: db "fire: orthographic", 0

pyramid:
        dw 5, 8                        ; five vertices, eight edges
        dw 65236, 0, 65236
        dw 300, 0, 65236
        dw 300, 0, 300
        dw 65236, 0, 300
        dw 0, 400, 0
        dw 0, 1
        dw 1, 2
        dw 2, 3
        dw 3, 0
        dw 0, 4
        dw 1, 4
        dw 2, 4
        dw 3, 4
