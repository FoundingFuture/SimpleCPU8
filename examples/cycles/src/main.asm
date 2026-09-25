; cycles: what every instruction costs, in microcycles, under this ROM's
; own microcode set (MY) and under the optimal set (OP).
;
; The build counts the rows, the fetch program plus the instruction's own,
; from src/microcode.txt and from the optimal set, and writes the lines
; into cycles.asm, which it generates and appends to this file. Change a
; row in microcode.txt, build, run: the MY column moves.
;
; Forty instructions a page, two columns of twenty. A key turns the page,
; and so do four seconds. cycles_n says how many there are.
        OUT    GPU_TEXT_COLOR, 0xFF
        OUT    GPU_TEXT_BG, 0
        OUT    GPU_TEXT_FLAGS, 0
        OUT    GPU_CMD, CMD_TEXT_STYLE
        LD     A <- 0
        LD     [page] <- A
show:   OUT    GPU_CMD, CMD_TEXT_CLEAR
        OUT    GPU_TEXT_COL, 0
        OUT    GPU_TEXT_ROW, 0
        OUT    GPU_CMD, CMD_TEXT_AT
        JSR    head
        OUT    GPU_TEXT_COL, 21
        OUT    GPU_TEXT_ROW, 0
        OUT    GPU_CMD, CMD_TEXT_AT
        JSR    head
        LD     A <- 0
        LD     [col] <- A
        LD     A <- 2
        LD     [row] <- A
        ; D1 walks the address table, three bytes an entry. Each page
        ; before this one skips forty entries and takes forty off left.
        LD     D1 <- &cycles_addr
        LD     A <- [cycles_n]
        LD     [left] <- A
        LD     A <- [page]
        JZ     fit
skip:   LD     [count] <- A
        LD     D1 <- D1+120
        LD     A <- [left]
        SUB    A <- 40
        LD     [left] <- A
        LD     A <- [count]
        DEC    A
        JNZ    skip
        ; More than forty left means another page follows this one.
fit:    LD     A <- 0
        LD     [more] <- A
        LD     A <- [left]
        CMP    A, 41
        JC     entry
        LD     A <- 1
        LD     [more] <- A
        LD     A <- 40
        LD     [left] <- A
entry:  LD     A <- [left]
        JZ     foot
        LD     A <- [col]
        OUTA   GPU_TEXT_COL
        LD     A <- [row]
        OUTA   GPU_TEXT_ROW
        OUT    GPU_CMD, CMD_TEXT_AT
        LD     A <- [D1]+
        OUTA   GPU_CART_BANK
        LD     A <- [D1]+
        OUTA   GPU_CART_HI
        LD     A <- [D1]+
        OUTA   GPU_CART_LO
        OUT    GPU_CMD, CMD_PRINTF
        LD     A <- [left]
        DEC    A
        LD     [left] <- A
        LD     A <- [row]
        INC    A
        LD     [row] <- A
        CMP    A, 22
        JNZ    entry
        LD     A <- 2
        LD     [row] <- A
        LD     A <- 21
        LD     [col] <- A
        JMP    entry
foot:   OUT    GPU_TEXT_COL, 0
        OUT    GPU_TEXT_ROW, 30
        OUT    GPU_CMD, CMD_TEXT_AT
        OUT    GPU_CART_BANK, get_bankbyte(footer)
        OUT    GPU_CART_HI, get_highbyte(footer)
        OUT    GPU_CART_LO, get_lowbyte(footer)
        OUT    GPU_CMD, CMD_PRINTF
        LD     A <- 0
        LD     [ticks] <- A
        IN     GPU_FRAME
        LD     [frame] <- A
; IN leaves the flags alone, so the byte is kept and tested with TST.
poll:   IN     IO_KEY -> A
        TST    A, 0x7F
        JZ     tick
        TST    A, 0x80
        JZ     flip
        JMP    poll
tick:   IN     GPU_FRAME
        CMP    A, [frame]
        JZ     poll
        LD     [frame] <- A
        LD     A <- [ticks]
        INC    A
        LD     [ticks] <- A
        CMP    A, 240
        JNZ    poll
flip:   LD     A <- [more]
        JZ     first
        LD     A <- [page]
        INC    A
        LD     [page] <- A
        JMP    show
first:  LD     A <- 0
        LD     [page] <- A
        JMP    show
head:   OUT    GPU_CART_BANK, get_bankbyte(header)
        OUT    GPU_CART_HI, get_highbyte(header)
        OUT    GPU_CART_LO, get_lowbyte(header)
        OUT    GPU_CMD, CMD_PRINTF
        RET
        .ram
page:   db     0
count:  db     0
more:   db     0
col:    db     0
row:    db     0
left:   db     0
frame:  db     0
ticks:  db     0
        .data
header: db     "INSTRUCTION   MY OP", 0
footer: db     "MY = MICROCODE.TXT   OP = OPTIMAL   KEY", 0
