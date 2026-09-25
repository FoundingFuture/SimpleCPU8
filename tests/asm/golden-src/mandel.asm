; The Mandelbrot set, drawn with complex arithmetic on the coprocessor.
;
; The CPU cannot multiply two bytes without a loop, and this needs a complex
; multiply of two 64-bit floats per iteration. That is the ACP's whole reason
; for existing: one OUT, one cycle, sixteen bytes in and sixteen out.
;
; z starts at zero and the rule is z = z*z + c, over and over. A point whose
; z stays small forever is IN the set and is drawn black. A point whose z runs
; away is outside, and the pass it escaped on picks its colour.
;
; The picture is drawn five times over, each one finer than the last: 16
; points across in 16 pixel squares, then 32, 64, 128, and finally 256 points
; in single pixels. The coarse picture is up in about a hundredth of the run,
; so there is nothing to wait for. Pause when it is sharp enough.
;
; The block, and why it is laid out this way. Every operand sits at a fixed
; offset from the block address, so the two commands are arranged to hand
; their results to each other:
;
;   multiply, block at z:    A = z at z, B = zc at z+16, R = zsq at z+32
;   add,      block at zsq:  A = zsq,    B = c at zsq+16, R = znew at zsq+32
;
; That works out to one contiguous run of five complex slots. The multiply
; needs its operand twice, at A and at B, so the copy at the end of each pass
; writes znew into BOTH z and zc. There is no way around that: the device has
; no state between commands, on purpose, so a value it needs twice has to be
; in memory twice.

        OUT GPU_COLOR, 0
        OUT GPU_CMD, CMD_CLEAR

; The GPU's HI latches are sticky and nothing here uses them, so clear them
; once. Forgetting this is the classic bug on this machine.

; ---- the picture, refined five times ----
;
; Each level doubles the grid and halves the block, so the first picture is
; up almost at once in 16 pixel squares and every pass after it sharpens:
; 16, 32, 64, 128, then 256 points across, drawn in 16, 8, 4, 2 and finally
; 1 pixel squares. Pause whenever it is sharp enough.
;
; Doubling the grid and halving the step lands the new points on exactly the
; plane coordinates the old ones had, plus the ones between. So a point with
; an even index in BOTH axes was computed last level, and the top left
; quarter of the square it painted then is already the colour it wants now.
; Skipping those is a quarter of the work at every level after the first.

        LD A <- 0
        LD [lvl] <- A

level:  JSR setlevel                   ; grid, block, shift and step

        LD A <- 0
        LD [py] <- A

; Every row starts at the left edge, so the imaginary part is set from the
; row and the real part is reset from xmin.
row:    LD A <- 0
        LD [px] <- A

col:    JSR skipped                    ; already painted by the last level?
        JZ nextcol                     ; Z set means yes, so leave it alone
        JSR setc                       ; c = xmin + px*step, ymin + py*step
        JSR escape                     ; how many passes before z ran away
        JSR plot

nextcol: LD A <- [px]
        ADD A <- 1
        LD [px] <- A
; The grid count is a byte, and 256 does not fit one. It is stored as zero,
; which makes this compare read "when px wraps" at the finest level and
; "when px reaches the count" at every other.
        SUB A <- [pn]
        JZ nextrow
        JMP col

nextrow: LD A <- [py]
        ADD A <- 1
        LD [py] <- A
        SUB A <- [pn]
        JZ nextlvl
        JMP row

nextlvl: LD A <- [lvl]
        ADD A <- 1
        LD [lvl] <- A
        SUB A <- 5
        JZ done
        JMP level
done:   HLT

; ---- one level's shape ----
;
; Four numbers come out of four tables, and the step comes out of a fifth as
; eight bytes. lvl indexes all five.
setlevel: LD D1 <- &pntab
        LD A <- [lvl]
        LD A <- [D1+A]
        LD [pn] <- A
        LD D1 <- &pbtab
        LD A <- [lvl]
        LD A <- [D1+A]
        LD [pblock] <- A
        LD D1 <- &pstab
        LD A <- [lvl]
        LD A <- [D1+A]
        LD [pshift] <- A

; The step is a float, so it moves eight bytes at a time. lvl times eight is
; three doublings, which is the only multiply this machine has.
        LD A <- [lvl]
        ADD A <- [lvl]
        LD [t2] <- A
        ADD A <- [t2]
        LD [t2] <- A
        ADD A <- [t2]
        LD [t2] <- A                   ; t2 = lvl * 8

        LD D1 <- &steptab
stwalk: LD A <- [t2]
        JZ stcopy
        INC D1
        SUB A <- 1
        LD [t2] <- A
        JMP stwalk

stcopy: LD D2 <- &step
        LD A <- 8
        LD [t3] <- A
stbyte: LD A <- [D1]+
        LD [D2]+ <- A
        LD A <- [t3]
        SUB A <- 1
        LD [t3] <- A
        JZ stdone
        JMP stbyte
stdone: RET

; ---- was this point painted by the level before it? ----
;
; Only if both indices are even, and only from the second level on. Returns
; with Z set when the point can be skipped.
skipped: LD A <- [lvl]
        JZ nope                        ; the first level paints everything
        LD A <- [px]
        AND A <- 1
        JZ maybe
nope:   LD A <- 1                      ; any nonzero: do the work
        AND A <- 1
        RET
maybe:  LD A <- [py]
        AND A <- 1
        RET

; ---- c = (xmin + px*step) + (ymin + py*step)i ----
;
; The screen column is an integer and the plane wants a float, so ACP_CVT
; does the conversion the CPU has no instruction for. One scratch block does
; both halves, one after the other.
setc:   LD A <- [px]
        JSR axis                       ; scratch holds xmin + px*step
        LD D1 <- [sres]
        LD [cre] <- D1
        LD D1 <- [sres+2]
        LD [cre+2] <- D1
        LD D1 <- [sres+4]
        LD [cre+4] <- D1
        LD D1 <- [sres+6]
        LD [cre+6] <- D1

        LD A <- [py]
        JSR ayis                       ; sresY holds ymin + py*step
        LD D1 <- [sresY]
        LD [cim] <- D1
        LD D1 <- [sresY+2]
        LD [cim+2] <- D1
        LD D1 <- [sresY+4]
        LD [cim+4] <- D1
        LD D1 <- [sresY+6]
        LD [cim+6] <- D1
        RET

; A pixel index in A becomes a coordinate in sres. The integer goes in as
; eight big-endian bytes, of which only the last can be nonzero at 64 wide.
axis:   LD [sint+7] <- A
        JSR toflt                      ; sflt = (float)sint
        OUT ACP_FMT, ACP_F64
        OUT ACP_ADDR_HI, sflt >> 8     ; A = sflt, B = step, R = sprod
        OUT ACP_ADDR_LO, sflt & 255
        OUT ACP_CMD, ACP_MUL
        OUT ACP_ADDR_HI, sprod >> 8    ; A = sprod, B = xmin, R = sres
        OUT ACP_ADDR_LO, sprod & 255
        OUT ACP_CMD, ACP_ADD
        RET

; The same, with ymin. The second operand of the add is the only difference,
; so the two routines share everything but which constant follows sprod.
ayis:   LD [sint+7] <- A
        JSR toflt
        OUT ACP_FMT, ACP_F64
        OUT ACP_ADDR_HI, sflt >> 8
        OUT ACP_ADDR_LO, sflt & 255
        OUT ACP_CMD, ACP_MUL
; The add's second operand is fixed at eight bytes past its first, so the
; product moves to a block whose neighbour is ymin rather than xmin.
        LD D1 <- [sprod]
        LD [sprodY] <- D1
        LD D1 <- [sprod+2]
        LD [sprodY+2] <- D1
        LD D1 <- [sprod+4]
        LD [sprodY+4] <- D1
        LD D1 <- [sprod+6]
        LD [sprodY+6] <- D1
        OUT ACP_ADDR_HI, sprodY >> 8
        OUT ACP_ADDR_LO, sprodY & 255
        OUT ACP_CMD, ACP_ADD
        RET

; sint, a signed 64-bit integer, becomes sflt, a double. The operand type and
; the result type differ, which is what ACP_RFMT is for.
toflt:  OUT ACP_FMT, ACP_I64
        OUT ACP_RFMT, ACP_F64
        OUT ACP_ADDR_HI, sint >> 8
        OUT ACP_ADDR_LO, sint & 255
        OUT ACP_CMD, ACP_CVT
        RET

; ---- the escape loop ----
;
; Leaves the pass number in [iter]. Reaching the limit means the point did not
; escape, and the caller draws it black.
escape: JSR zeroz
        LD A <- 0
        LD [iter] <- A

; The pass number counts up BEFORE the test, so a point that runs away on the
; very first pass reads 1 rather than 0. Zero is reserved for a point that
; never escaped, and those are the black ones.
pass:   LD A <- [iter]
        ADD A <- 1
        LD [iter] <- A

        OUT ACP_FMT, ACP_C64           ; z*z into zsq
        OUT ACP_ADDR_HI, z >> 8
        OUT ACP_ADDR_LO, z & 255
        OUT ACP_CMD, ACP_MUL

        OUT ACP_ADDR_HI, zsq >> 8      ; zsq + c into znew
        OUT ACP_ADDR_LO, zsq & 255
        OUT ACP_CMD, ACP_ADD

        OUT ACP_RFMT, ACP_F64          ; |znew|, a real out of a complex pair
        OUT ACP_ADDR_HI, znew >> 8
        OUT ACP_ADDR_LO, znew & 255
        OUT ACP_CMD, ACP_ABS

        OUT ACP_FMT, ACP_F64           ; is it past 2 yet?
        OUT ACP_ADDR_HI, mag >> 8
        OUT ACP_ADDR_LO, mag & 255
        OUT ACP_CMD, ACP_CMP

; ACP_CMP writes minus one, zero or one, and the device already knows the
; sign. Reading it back off the flags costs one IN and one AND, where reading
; the float would cost a load and knowing which byte holds the sign bit.
        IN ACP_FLAGS -> A
        AND A <- ACP_NEGATIVE
        JZ gone                        ; not negative: |z| is 2 or more

        JSR backz                      ; znew becomes z and zc for the next pass
        LD A <- [iter]
        SUB A <- 24
        JZ inside
        JMP pass

gone:   RET
inside: LD A <- 0                      ; the limit stands for "never escaped"
        LD [iter] <- A
        RET

; z and its copy both go to zero. The multiply reads its operand at two
; addresses, so both slots are cleared.
zeroz:  LD D1 <- &z
        LD A <- 32
        LD [n] <- A
zloop:  LD A <- 0
        LD [D1]+ <- A
        LD A <- [n]
        SUB A <- 1
        LD [n] <- A
        JZ zdone
        JMP zloop
zdone:  RET

; znew into z, then znew into zc. Sixteen bytes each way.
backz:  LD D1 <- &znew
        LD D2 <- &z
        JSR c16
        LD D1 <- &znew
        LD D2 <- &zc
        JSR c16
        RET

c16:    LD A <- 16
        LD [n] <- A
cloop:  LD A <- [D1]+
        LD [D2]+ <- A
        LD A <- [n]
        SUB A <- 1
        LD [n] <- A
        JZ cdone
        JMP cloop
cdone:  RET

; ---- one 4 by 4 block on the screen ----

plot:   LD A <- [iter]
        JZ black
        LD D1 <- &palette
        LD A <- [iter]
        LD A <- [D1+A]
        OUTA GPU_COLOR
        OUT GPU_CMD, CMD_SET_COLOR
        JMP box
black:  OUT GPU_COLOR, 0
        OUT GPU_CMD, CMD_SET_COLOR

; The square is block wide, so a point at index px starts at px times block.
; block is a power of two, so that is a run of doublings and pshift says how
; many. x + x is x << 1, and this machine has no other shift.
box:    LD A <- [px]                   ; the pen goes to the top left corner
        LD [t0] <- A
        JSR shl
        LD A <- [t0]
        LD [t4] <- A                   ; keep the left edge for the far corner
        OUTA GPU_X
        LD A <- [py]
        LD [t0] <- A
        JSR shl
        LD A <- [t0]
        LD [t1] <- A
        OUTA GPU_Y
        OUT GPU_CMD, CMD_MOVE_TO
        LD A <- [t4]
        ADD A <- [pblock]
        SUB A <- 1
        OUTA GPU_X
        LD A <- [t1]
        ADD A <- [pblock]
        SUB A <- 1
        OUTA GPU_Y
        OUT GPU_CMD, CMD_RECT
        RET

; t0 doubled pshift times. At the finest level pshift is zero and this does
; nothing at all, which is what a one pixel square wants.
shl:    LD A <- [pshift]
        LD [t5] <- A
shlnext: LD A <- [t5]
        JZ shldone
        LD A <- [t0]
        ADD A <- [t0]
        LD [t0] <- A
        LD A <- [t5]
        SUB A <- 1
        LD [t5] <- A
        JMP shlnext
shldone: RET

.ram
; The refinement level, and the shape it gives this pass.
lvl:    db 0
pn:     db 0                           ; points across, 0 meaning 256
pblock: db 0                           ; pixels per square
pshift: db 0                           ; log2 of pblock
t2:     db 0
t3:     db 0
t4:     db 0
t5:     db 0

; Five levels. The grid doubles, the square halves, and 256 is stored as the
; zero it becomes in a byte.
pntab:  db 16, 32, 64, 128, 0
pbtab:  db 16, 8, 4, 2, 1
pstab:  db 4, 3, 2, 1, 0

; 3 divided by the grid, one double per level, so the picture always spans
; the same three units of the plane.
steptab: db $3F,$C8,$00,$00,$00,$00,$00,$00
        db $3F,$B8,$00,$00,$00,$00,$00,$00
        db $3F,$A8,$00,$00,$00,$00,$00,$00
        db $3F,$98,$00,$00,$00,$00,$00,$00
        db $3F,$88,$00,$00,$00,$00,$00,$00
; The iteration block, one contiguous run. The offsets are the whole design:
; z at the multiply's A, zc at its B, zsq at its R and at the add's A, c at
; the add's B, and znew at the add's R.
z:      db 0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0
zc:     db 0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0
zsq:    db 0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0
cre:    db 0,0,0,0,0,0,0,0
cim:    db 0,0,0,0,0,0,0,0
znew:   db 0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0

; The escape test's own little block: A is the magnitude, B is two, and the
; comparison lands after them.
mag:    db 0,0,0,0,0,0,0,0
two:    db $40,$00,$00,$00,$00,$00,$00,$00      ; 2.0
cmpres: db 0,0,0,0,0,0,0,0

; The coordinate block. sint converts to sflt, sflt times step is sprod, and
; sprod plus xmin is sres. sprodY is the same slot read as the start of the
; other add, whose second operand is ymin.
sint:   db 0,0,0,0,0,0,0,0
sflt:   db 0,0,0,0,0,0,0,0
step:   db $3F,$A8,$00,$00,$00,$00,$00,$00      ; 3/64
sprod:  db 0,0,0,0,0,0,0,0
xmin:   db $C0,$00,$00,$00,$00,$00,$00,$00      ; -2.0
sres:   db 0,0,0,0,0,0,0,0
sprodY: db 0,0,0,0,0,0,0,0
ymin:   db $BF,$F8,$00,$00,$00,$00,$00,$00      ; -1.5
sresY:  db 0,0,0,0,0,0,0,0

px:     db 0
py:     db 0
iter:   db 0
n:      db 0
t0:     db 0
t1:     db 0
palette: db 0
        db $E0,$E4,$E8,$EC,$F0,$F4,$B4,$94
        db $74,$54,$34,$38,$3C,$1C,$1D,$1E
        db $1F,$3F,$5F,$7F,$9F,$BF,$DF,$FF
