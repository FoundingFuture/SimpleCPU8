; The screen is a peripheral: OUT and OUTA talk to it through ports.
; First paint a horizontal gradient (color = x), then rewrite the whole
; palette to grayscale and watch the same pixels change color.
; The data ports clear behind every command, so a plot would forget y before
; the next one. MOD_STICKY holds them, which is what it is for: repeating a
; command with one argument changed.
        OUT GPU_CMD_MOD, MOD_STICKY
        LD A <- 0
        LD [y] <- A
yloop:  LD A <- [y]
        OUTA GPU_Y
        LD A <- 0
        LD [x] <- A
xloop:  LD A <- [x]
        OUTA GPU_X
        OUTA GPU_PIXEL      ; color = x, one band per palette entry
        OUT GPU_CMD, CMD_PLOT
        LD A <- [x]
        INC A
        LD [x] <- A
        JZ nextrow          ; x wrapped: this row is done
        JMP xloop
nextrow: LD A <- [y]
        INC A
        LD [y] <- A
        JZ recolor          ; y wrapped: the screen is full
        JMP yloop

; Grayscale: entry i becomes (i, i, i). A palette is 768 bytes, so it moves
; through memory rather than a byte at a time through a port. Build it with
; a pointer walk, then one command installs the whole thing.
;
; This is a route the machine did not have. A cartridge palette is written
; once at assembly time. A palette built with arithmetic can only come from
; RAM, and now it can.
recolor: OUT GPU_CMD_MOD, 0   ; back to clearing, now the gradient is done
        LD D1 <- palbuf
        LD A <- 0
        LD [i] <- A
ploop:  LD A <- [i]
        LD [D1]+ <- A       ; red
        LD [D1]+ <- A       ; green
        LD [D1]+ <- A       ; blue
        LD A <- [i]
        INC A
        LD [i] <- A
        JZ install
        JMP ploop

install: OUT GPU_MAP, MAP_PALETTE_IN
        OUT GPU_MAP_HI, palbuf >> 8
        OUT GPU_MAP_LO, palbuf & 255
        OUT GPU_CMD, CMD_MEMMAP
        OUT GPU_CMD, CMD_FETCH_PALETTE
done:   HLT

.ram
x: db 0
y: db 0
i: db 0
; 768 bytes for the palette, named without storing anything there.
palbuf: .addr($1000)
