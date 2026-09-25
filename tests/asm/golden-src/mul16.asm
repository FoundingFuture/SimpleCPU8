; Multiply two 16-bit words into a 32-bit product, on a machine whose
; widest register holds eight bits.
;
; Nothing new is needed. mul8 built a 16-bit product out of byte adds by
; letting one carry walk upward; this walks the same carry over four bytes
; instead of two. ADD opens the chain with carry-in forced to 0, ADC
; continues it, and the byte loads and stores in between leave C alone.
;
; Words are big-endian everywhere on this machine, so a 32-bit value is
; p0 p1 p2 p3 with p0 the most significant. The chain therefore runs from
; p3 upward, which is why the doubling below starts at the bottom of the
; block and ends at the top.
;
; Sixteen passes, one per multiplier bit, most significant first. Each pass
; doubles the product, then shifts a copy of the multiplier left. Whatever
; falls out of bit 15 is that pass's bit, and C set means add the
; multiplicand in.
;
; %lu prints four bytes. The length is what sets the width: %hhu is a byte,
; %u a word, %lu a long. Push the bytes high first, like every other word.

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

; Walk the operand table. Four bytes in, one line out.
        LD D1 <- &pairs
        LD A <- 9                      ; first result row
        LD [row] <- A
        LD A <- [npairs]
        LD [left] <- A

line:   LD A <- [D1]+
        LD [mc0] <- A
        LD A <- [D1]+
        LD [mc1] <- A
        LD A <- [D1]+
        LD [mp0] <- A
        LD A <- [D1]+
        LD [mp1] <- A
        JSR mul16

        LD A <- [row]
        OUTA GPU_TEXT_ROW
        OUT GPU_TEXT_COL, 2
        OUT GPU_CMD, CMD_TEXT_AT
        LD A <- [mc0]                  ; each word goes high byte first
        LD [args+0] <- A
        LD A <- [mc1]
        LD [args+1] <- A
        LD A <- [mp0]
        LD [args+2] <- A
        LD A <- [mp1]
        LD [args+3] <- A
        LD A <- [p0]                   ; and the long goes high byte first
        LD [args+4] <- A
        LD A <- [p1]
        LD [args+5] <- A
        LD A <- [p2]
        LD [args+6] <- A
        LD A <- [p3]
        LD [args+7] <- A
        OUT GPU_CART_BANK, get_bankbyte(ans)
        OUT GPU_CART_HI, get_highbyte(ans)
        OUT GPU_CART_LO, get_lowbyte(ans)
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

; mul16: [p0 p1 p2 p3] = [mc0 mc1] * [mp0 mp1]. Both operands survive the
; call, so the caller can still print them. 16 by 16 never overflows 32
; bits, so nothing is lost and there is no carry left over at the top.
mul16:  LD A <- 0
        LD [p0] <- A
        LD [p1] <- A
        LD [p2] <- A
        LD [p3] <- A
        LD A <- [mp0]
        LD [m0] <- A                   ; shift a copy, keep the operand
        LD A <- [mp1]
        LD [m1] <- A
        LD A <- 16
        LD [m16n] <- A

        ; product <<= 1, four bytes, one carry chain from the bottom up
m16lp:  LD A <- [p3]
        ADD A <- [p3]                  ; the lowest byte opens the chain
        LD [p3] <- A
        LD A <- [p2]
        ADC A <- [p2]
        LD [p2] <- A
        LD A <- [p1]
        ADC A <- [p1]
        LD [p1] <- A
        LD A <- [p0]
        ADC A <- [p0]
        LD [p0] <- A

        ; multiplier <<= 1, C takes the bit that left bit 15
        LD A <- [m1]
        ADD A <- [m1]
        LD [m1] <- A
        LD A <- [m0]
        ADC A <- [m0]
        LD [m0] <- A
        JC m16add
        JMP m16nx

        ; that bit was set, so add the multiplicand into the low half and
        ; let the carry walk up through the high half
m16add: LD A <- [p3]
        ADD A <- [mc1]
        LD [p3] <- A
        LD A <- [p2]
        ADC A <- [mc0]
        LD [p2] <- A
        LD A <- [p1]
        ADC A <- 0                     ; nothing to add but the carry
        LD [p1] <- A
        LD A <- [p0]
        ADC A <- 0
        LD [p0] <- A

m16nx:  LD A <- [m16n]
        SUB A <- 1
        LD [m16n] <- A
        JZ m16end
        JMP m16lp
m16end: RET

.ram
; printf reads its arguments from RAM, so they are stored rather than
; pushed a byte at a time through a port.
args:   db 0, 0, 0, 0, 0, 0, 0, 0
mc0:    db 0                   ; the multiplicand, high byte first
mc1:    db 0
mp0:    db 0                   ; the multiplier
mp1:    db 0
p0:     db 0                   ; the product, four bytes, high first
p1:     db 0
p2:     db 0
p3:     db 0
m0:     db 0                   ; the multiplier copy that gets shifted
m1:     db 0
m16n:   db 0
row:    db 0
left:   db 0
npairs: db 7
pairs:  dw 1000,1000
        dw 65535,65535
        dw 300,257
        dw 65535,1
        dw 1,65535
        dw 12345,6789
        dw 256,256

.data
title:  db "16 x 16 multiply", 0
sub:    db "one carry over four bytes", 0
hint:   db "%%lu prints a long", 0
ans:    db "%5u*%5u=%10lu", 0
