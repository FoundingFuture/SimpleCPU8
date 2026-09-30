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
; x lives in A for the whole row. OUT and OUTA leave A alone, so nothing
; stores it and nothing loads it back.
        LD A <- 0
xloop:  OUTA GPU_X
        OUTA GPU_PIXEL      ; color = x, one band per palette entry
        OUT GPU_CMD, CMD_PLOT
        INC A
        JNZ xloop           ; x wrapped: this row is done
        LD A <- [y]
        INC A
        LD [y] <- A
        JNZ yloop           ; y wrapped: the screen is full

; Grayscale: entry i becomes (i, i, i). A palette is 768 bytes, so it moves
; through memory rather than a byte at a time through a port. Build it with
; a pointer walk, then one command installs the whole thing.
;
; This is a route the machine did not have. A cartridge palette is written
; once at assembly time. A palette built with arithmetic can only come from
; RAM, and now it can.
recolor: OUT GPU_CMD_MOD, 0   ; back to clearing, now the gradient is done
        LD D1 <- palbuf
        LD A <- 0           ; i, kept in A the same way
ploop:  LD [D1]+ <- A       ; red
        LD [D1]+ <- A       ; green
        LD [D1]+ <- A       ; blue
        INC A
        JNZ ploop

install: OUT GPU_MAP, MAP_PALETTE_IN
        OUT GPU_MAP_HI, palbuf >> 8
        OUT GPU_MAP_LO, palbuf & 255
        OUT GPU_CMD, CMD_MEMMAP
        OUT GPU_CMD, CMD_FETCH_PALETTE
done:   HLT

.ram
y: db 0
; 768 bytes for the palette, named without storing anything there.
palbuf: .addr($1000)
