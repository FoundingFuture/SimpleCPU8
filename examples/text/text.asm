; printf: formatted text drawn over the graphics.
; The GPU keeps a character plane above the pixels. printf reads a
; template from ROM, formats it with parameters read from RAM, and draws
; it at the text cursor in the text color. The star here is one byte,
; 200, read four different ways. The bits never change. The format does.
;
; GPU_TEXT_COLOR paints the whole character plane, not the next line.
; Each write below recolors the text already on screen, so the last one
; wins and the finished picture is magenta throughout.

        OUT GPU_COLOR, 1
        OUT GPU_CMD, CMD_CLEAR        ; a dark background to sit on

; Title.
        OUT GPU_TEXT_COLOR, $FC
        OUT GPU_CMD, CMD_TEXT_STYLE
        OUT GPU_TEXT_COL, 1
        OUT GPU_TEXT_ROW, 1
        OUT GPU_CMD, CMD_TEXT_AT
        OUT GPU_CART_BANK, get_bankbyte(title)
        OUT GPU_CART_HI, get_highbyte(title)
        OUT GPU_CART_LO, get_lowbyte(title)
        OUT GPU_TEXT_ARG_HI, args >> 8
        OUT GPU_TEXT_ARG_LO, args & 255
        OUT GPU_CMD, CMD_PRINTF

; The same byte four ways. Each line pushes 200, then prints.
        OUT GPU_TEXT_COLOR, $1C
        OUT GPU_CMD, CMD_TEXT_STYLE
        OUT GPU_TEXT_COL, 2
        OUT GPU_TEXT_ROW, 3
        OUT GPU_CMD, CMD_TEXT_AT
        LD A <- 200
        LD [args+0] <- A
        OUT GPU_CART_BANK, get_bankbyte(t1)
        OUT GPU_CART_HI, get_highbyte(t1)
        OUT GPU_CART_LO, get_lowbyte(t1)
        OUT GPU_TEXT_ARG_HI, args >> 8
        OUT GPU_TEXT_ARG_LO, args & 255
        OUT GPU_CMD, CMD_PRINTF

        OUT GPU_TEXT_COL, 2
        OUT GPU_TEXT_ROW, 4
        OUT GPU_CMD, CMD_TEXT_AT
        LD A <- 200
        LD [args+0] <- A
        OUT GPU_CART_BANK, get_bankbyte(t2)
        OUT GPU_CART_HI, get_highbyte(t2)
        OUT GPU_CART_LO, get_lowbyte(t2)
        OUT GPU_TEXT_ARG_HI, args >> 8
        OUT GPU_TEXT_ARG_LO, args & 255
        OUT GPU_CMD, CMD_PRINTF

        OUT GPU_TEXT_COL, 2
        OUT GPU_TEXT_ROW, 5
        OUT GPU_CMD, CMD_TEXT_AT
        LD A <- 200
        LD [args+0] <- A
        OUT GPU_CART_BANK, get_bankbyte(t3)
        OUT GPU_CART_HI, get_highbyte(t3)
        OUT GPU_CART_LO, get_lowbyte(t3)
        OUT GPU_TEXT_ARG_HI, args >> 8
        OUT GPU_TEXT_ARG_LO, args & 255
        OUT GPU_CMD, CMD_PRINTF

        OUT GPU_TEXT_COL, 2
        OUT GPU_TEXT_ROW, 6
        OUT GPU_CMD, CMD_TEXT_AT
        LD A <- 200
        LD [args+0] <- A
        OUT GPU_CART_BANK, get_bankbyte(t4)
        OUT GPU_CART_HI, get_highbyte(t4)
        OUT GPU_CART_LO, get_lowbyte(t4)
        OUT GPU_TEXT_ARG_HI, args >> 8
        OUT GPU_TEXT_ARG_LO, args & 255
        OUT GPU_CMD, CMD_PRINTF

; A 16-bit word and a float, showing width and precision.
        OUT GPU_TEXT_COLOR, $1F
        OUT GPU_CMD, CMD_TEXT_STYLE
        OUT GPU_TEXT_COL, 2
        OUT GPU_TEXT_ROW, 8
        OUT GPU_CMD, CMD_TEXT_AT
        LD D2 <- $1234                 ; a word goes in whole, high byte first
        LD [args+0] <- D2
        LD D2 <- $4048
        LD [args+2] <- D2
        LD D2 <- $F5C3
        LD [args+4] <- D2
        OUT GPU_CART_BANK, get_bankbyte(t5)
        OUT GPU_CART_HI, get_highbyte(t5)
        OUT GPU_CART_LO, get_lowbyte(t5)
        OUT GPU_TEXT_ARG_HI, args >> 8
        OUT GPU_TEXT_ARG_LO, args & 255
        OUT GPU_CMD, CMD_PRINTF

; A string whose bytes live in RAM, printed with %s. The template is in
; ROM, but %s follows a pointer into RAM. %5r prints five raw bytes.
        OUT GPU_TEXT_COLOR, $E3        ; magenta, and so is everything above
        OUT GPU_CMD, CMD_TEXT_STYLE
        OUT GPU_TEXT_COL, 2
        OUT GPU_TEXT_ROW, 10
        OUT GPU_CMD, CMD_TEXT_AT
        LD D2 <- name                  ; the string's address, twice
        LD [args+0] <- D2
        LD [args+2] <- D2
        OUT GPU_CART_BANK, get_bankbyte(t6)
        OUT GPU_CART_HI, get_highbyte(t6)
        OUT GPU_CART_LO, get_lowbyte(t6)
        OUT GPU_TEXT_ARG_HI, args >> 8
        OUT GPU_TEXT_ARG_LO, args & 255
        OUT GPU_CMD, CMD_PRINTF
        HLT

.ram
; printf reads its arguments from RAM now, so they are stored here rather
; than pushed a byte at a time through a port. Their number is no longer
; bounded by how many OUTs a program is willing to write.
args: db 0, 0, 0, 0, 0, 0
name: db "Eddie", 0

.data
title: db "SimpleCPU-8 printf", 0
t1:    db "200 unsigned: %hhu", 0
t2:    db "200 signed:   %hhd", 0
t3:    db "200 hex:      %#hhx", 0
t4:    db "200 binary:   %hhb", 0
t5:    db "word %u   pi = %.2f", 0
t6:    db "hi %s (raw %5r)", 0
