; Multiply two bytes into a 16-bit product, with no multiply instruction
; and no shift instruction either.
;
; Shift-left is addition. x + x is x << 1, and the bit pushed off the top
; lands in C. ADD opens the carry chain with carry-in forced to 0, ADC
; carries it upward, and the byte loads and stores in between leave C
; alone, so one carry walks from prodlo up into prodhi.
;
; mul8 is shift-and-add, most significant bit first. Each pass doubles the
; product, then shifts a copy of the multiplier left. Whatever falls out of
; bit 7 is that pass's multiplier bit. C set means add mcand in.
;
; The table below drives it, one product and one printed line per pass.
; Each line pushes two bytes and a 16-bit word, so the template reads
; %hhu for a byte and %u for a word.

        OUT GPU_COLOR, 1
        OUT GPU_CMD, CMD_CLEAR

; GPU_TEXT_COLOR paints the whole character plane, not the next line, so
; set it once. Writing it again would recolor the text already on screen.
        OUT GPU_TEXT_COLOR, $1C        ; green
        OUT GPU_CMD, CMD_TEXT_STYLE
        OUT GPU_CMD, CMD_TEXT_AT

        OUT GPU_TEXT_COL, 2
        OUT GPU_TEXT_ROW, 2
        OUT GPU_CMD, CMD_TEXT_AT
        OUT GPU_CART_BANK, get_bankbyte(title)
        OUT GPU_CART_HI, get_highbyte(title)
        OUT GPU_CART_LO, get_lowbyte(title)
        OUT GPU_TEXT_ARG_HI, args >> 8
        OUT GPU_TEXT_ARG_LO, args & 255
        OUT GPU_CMD, CMD_PRINTF

        OUT GPU_TEXT_COL, 2
        OUT GPU_TEXT_ROW, 4
        OUT GPU_CMD, CMD_TEXT_AT
        OUT GPU_CART_BANK, get_bankbyte(sub)
        OUT GPU_CART_HI, get_highbyte(sub)
        OUT GPU_CART_LO, get_lowbyte(sub)
        OUT GPU_TEXT_ARG_HI, args >> 8
        OUT GPU_TEXT_ARG_LO, args & 255
        OUT GPU_CMD, CMD_PRINTF

        OUT GPU_TEXT_COL, 2
        OUT GPU_TEXT_ROW, 6
        OUT GPU_CMD, CMD_TEXT_AT
        OUT GPU_CART_BANK, get_bankbyte(hint)
        OUT GPU_CART_HI, get_highbyte(hint)
        OUT GPU_CART_LO, get_lowbyte(hint)
        OUT GPU_TEXT_ARG_HI, args >> 8
        OUT GPU_TEXT_ARG_LO, args & 255
        OUT GPU_CMD, CMD_PRINTF

; Walk the operand table. Two bytes in, one line out.
        LD D1 <- &pairs
        LD A <- 9                      ; first result row
        LD [row] <- A
        LD A <- [npairs]
        LD [left] <- A

line:   LD A <- [D1]+
        LD [mcand] <- A
        LD A <- [D1]+
        LD [mplier] <- A
        JSR mul8

        LD A <- [row]
        OUTA GPU_TEXT_ROW
        OUT GPU_TEXT_COL, 4
        OUT GPU_CMD, CMD_TEXT_AT
        LD A <- [mcand]
        LD [args+0] <- A
        LD A <- [mplier]
        LD [args+1] <- A
        LD A <- [prodhi]               ; the word goes high byte first
        LD [args+2] <- A
        LD A <- [prodlo]
        LD [args+3] <- A
        OUT GPU_CART_BANK, get_bankbyte(sum)
        OUT GPU_CART_HI, get_highbyte(sum)
        OUT GPU_CART_LO, get_lowbyte(sum)
        OUT GPU_TEXT_ARG_HI, args >> 8
        OUT GPU_TEXT_ARG_LO, args & 255
        OUT GPU_CMD, CMD_PRINTF

        LD A <- [row]
        ADD A <- 2                     ; every other row, so lines breathe
        LD [row] <- A
        LD A <- [left]
        SUB A <- 1
        LD [left] <- A
        JZ done
        JMP line
done:   HLT

; mul8: [prodhi][prodlo] = [mcand] * [mplier]. Both operands survive the
; call, so the caller can still print them.
mul8:   LD A <- 0
        LD [prodhi] <- A
        LD [prodlo] <- A
        LD A <- [mplier]
        LD [m8m] <- A                  ; shift a copy, keep the operand
        LD A <- 8
        LD [m8n] <- A

        ; product <<= 1
m8lp:   LD A <- [prodlo]
        ADD A <- [prodlo]              ; low byte opens the carry chain
        LD [prodlo] <- A
        LD A <- [prodhi]
        ADC A <- [prodhi]              ; C rode over the load and the store
        LD [prodhi] <- A

        ; multiplier <<= 1, C takes the bit that left bit 7
        LD A <- [m8m]
        ADD A <- [m8m]
        LD [m8m] <- A
        JC m8add
        JMP m8nx

        ; that bit was set, so add the multiplicand in
m8add:  LD A <- [prodlo]
        ADD A <- [mcand]
        LD [prodlo] <- A
        LD A <- [prodhi]
        ADC A <- 0                     ; nothing to add but the carry
        LD [prodhi] <- A

m8nx:   LD A <- [m8n]
        SUB A <- 1
        LD [m8n] <- A
        JZ m8end
        JMP m8lp
m8end:  RET

.ram
; printf reads its arguments from RAM, so they are stored rather than
; pushed a byte at a time through a port.
args:   db 0, 0, 0, 0, 0, 0, 0, 0
mcand:  db 0
mplier: db 0
prodhi: db 0                   ; big-endian and adjacent, so after the call
prodlo: db 0                   ; LD D1 <- [prodhi] picks up all 16 bits
m8m:    db 0
m8n:    db 0
row:    db 0
left:   db 0
npairs: db 8
pairs:  db 7,13, 12,12, 128,2, 1,255, 0,255, 100,100, 200,173, 255,255

.data
title:  db "8 x 8 multiply", 0
sub:    db "no multiply op, no shift op", 0
hint:   db "x + x is x << 1", 0
sum:    db "%3hhu * %3hhu = %5u", 0
