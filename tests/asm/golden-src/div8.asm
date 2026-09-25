; Divide one byte by another, with no divide instruction and no shift
; instruction either.
;
; Shift-left is addition. x + x is x << 1, and the bit pushed off the top
; lands in C. ADC A <- x after LD A <- x is a rotate left through carry,
; because A + A + C is the old A doubled with C landing in bit 0. The byte
; loads and stores in between leave C alone, so a carry walks between bytes.
;
; div8 is restoring division, most significant bit first. The quotient is
; built in the same byte the dividend leaves: each pass shifts quot left,
; the bit that falls out of bit 7 rotates into rem, and a quotient bit is
; rotated in at the bottom. After eight passes the dividend is gone and the
; quotient has taken its place.
;
; SUB sets C as a BORROW. C set means the subtraction went negative, so the
; divisor did not fit and rem is put back untouched. C clear means it fit,
; so the difference is kept and the quotient bit is set.
;
; The remainder cannot outgrow its byte, and that is worth knowing rather
; than guarding against. Before the pass k doubling, rem holds the top k
; bits of the dividend reduced by the divisor, so it is at most 2^k - 1.
; The last doubling is k = 7, so the widest it ever reaches is 2 * 127 + 1,
; which is 255. The byte is always enough.
;
; That holds because the dividend and the divisor are the same width. A
; dividend WIDER than its divisor is the case that needs a ninth bit, and
; 16 / 8 is where you meet it. Proved on this program over all 65,280
; pairs, in div8.test.ts, rather than argued.

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
        LD [dvend] <- A
        LD A <- [D1]+
        LD [dsor] <- A
        JSR div8

        LD A <- [row]
        OUTA GPU_TEXT_ROW
        OUT GPU_TEXT_COL, 3
        OUT GPU_CMD, CMD_TEXT_AT
        LD A <- [dvend]
        LD [args+0] <- A
        LD A <- [dsor]
        LD [args+1] <- A
        LD A <- [quot]
        LD [args+2] <- A
        LD A <- [rem]
        LD [args+3] <- A
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

; div8: [quot] = [dvend] / [dsor], [rem] = the remainder. Both operands
; survive the call, so the caller can still print them.
;
; A divisor of zero is not refused. SUB A <- 0 never borrows, so every pass
; keeps and sets its bit: the quotient comes out $FF and the remainder is
; the dividend, untouched. That is the machine saying it could not divide,
; and the last table row shows it.
div8:   LD A <- [dvend]
        LD [quot] <- A                 ; the dividend shifts out of here
        LD A <- 0
        LD [rem] <- A
        LD A <- 8
        LD [d8n] <- A

        ; quot <<= 1, and the bit leaving bit 7 lands in C
d8lp:   LD A <- [quot]
        ADD A <- [quot]
        LD [quot] <- A

        ; rem = rem * 2 + C, a rotate left through carry
        LD A <- [rem]
        ADC A <- [rem]                 ; C rode over the load and the store
        LD [rem] <- A

        ; Does the divisor fit? Subtract and read the borrow. There is no
        ; CMP on this machine and none is wanted: the subtract has already
        ; done the work, and A holds the difference when it fits.
        SUB A <- [dsor]                ; A still holds rem
        JC d8nx                        ; borrow: it did not fit, rem stands
        LD [rem] <- A                  ; it fit, so keep the difference
        LD A <- [quot]
        OR A <- 1                      ; the quotient bit for this pass
        LD [quot] <- A

d8nx:   LD A <- [d8n]
        SUB A <- 1
        LD [d8n] <- A
        JZ d8end
        JMP d8lp
d8end:  RET

.ram
; printf reads its arguments from RAM, so they are stored rather than
; pushed a byte at a time through a port.
args:   db 0, 0, 0, 0, 0, 0, 0, 0
dvend:  db 0
dsor:   db 0
quot:   db 0
rem:    db 0
d8n:    db 0
row:    db 0
left:   db 0
npairs: db 8
pairs:  db 200,7, 255,16, 7,9, 255,1, 128,3, 255,254, 100,100, 5,0

.data
title:  db "8 / 8 divide", 0
sub:    db "no divide op, no shift op", 0
hint:   db "C is the borrow", 0
ans:    db "%3hhu / %3hhu = %3hhu r %3hhu", 0
