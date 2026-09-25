; Divide one 16-bit word by another, on a machine with no divide
; instruction, no shift instruction, and no compare instruction either.
;
; div8 did this a byte at a time. Widening it needs no new idea, only the
; carry walking one byte further, because every piece already spans bytes:
;
;   shift left   ADD on the low byte, ADC on the high byte
;   compare      SUB on the low byte, SBC on the high byte
;   the answer   C after the SBC, which is the borrow out of the pair
;
; That last line is the one worth stopping at. There is no CMP on this
; machine and none is wanted: a subtract already answers the question, and
; the answer is in C. C set means the pair went negative, so the divisor
; did not fit. The difference is computed into a scratch pair first, so a
; miss costs nothing to undo. Nothing is put back, because nothing moved.
;
; A store between the SUB and the SBC is what makes this legal. Byte loads
; and stores leave C alone, so the borrow survives the trip to memory.
;
; Sixteen passes, most significant bit first. The dividend shifts out of
; the quotient as the quotient shifts in, so one pair does both jobs.
;
; The remainder cannot outgrow its pair, for the reason div8's remainder
; cannot outgrow its byte. Before the pass k doubling it holds the top k
; bits of the dividend reduced by the divisor, so it is at most 2^k - 1,
; and the last doubling is k = 15. That gives 2 * 32767 + 1, which is
; 65535. Equal widths are what buy that. Divide a wider dividend by a
; narrower divisor and a seventeenth bit appears, needing a branch this
; routine does not have and does not need.

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
        LD [dv0] <- A
        LD A <- [D1]+
        LD [dv1] <- A
        LD A <- [D1]+
        LD [ds0] <- A
        LD A <- [D1]+
        LD [ds1] <- A
        JSR div16

        LD A <- [row]
        OUTA GPU_TEXT_ROW
        OUT GPU_TEXT_COL, 2
        OUT GPU_CMD, CMD_TEXT_AT
        LD A <- [dv0]                  ; every word goes high byte first
        LD [args+0] <- A
        LD A <- [dv1]
        LD [args+1] <- A
        LD A <- [ds0]
        LD [args+2] <- A
        LD A <- [ds1]
        LD [args+3] <- A
        LD A <- [q0]
        LD [args+4] <- A
        LD A <- [q1]
        LD [args+5] <- A
        LD A <- [r0]
        LD [args+6] <- A
        LD A <- [r1]
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

; div16: [q0 q1] = [dv0 dv1] / [ds0 ds1], [r0 r1] = the remainder. The
; dividend does not survive: the quotient is built in the pair it vacates.
;
; A divisor of zero is not refused. A subtract of zero never borrows, so
; every pass keeps and sets its bit: the quotient comes out $FFFF and the
; remainder is the dividend, whole. That is the machine saying it could not
; divide, and the last table row shows it.
div16:  LD A <- [dv0]
        LD [q0] <- A
        LD A <- [dv1]
        LD [q1] <- A
        LD A <- 0
        LD [r0] <- A
        LD [r1] <- A
        LD A <- 16
        LD [d16n] <- A

        ; quot <<= 1, and the bit leaving bit 15 lands in C
d16lp:  LD A <- [q1]
        ADD A <- [q1]
        LD [q1] <- A
        LD A <- [q0]
        ADC A <- [q0]
        LD [q0] <- A

        ; rem = rem * 2 + C, a rotate through carry across the pair
        LD A <- [r1]
        ADC A <- [r1]
        LD [r1] <- A
        LD A <- [r0]
        ADC A <- [r0]
        LD [r0] <- A

        ; Does the divisor fit? Subtract the pair into scratch, and read C
        ; after the SBC. That one bit is the whole answer.
        LD A <- [r1]
        SUB A <- [ds1]
        LD [t1] <- A                   ; the store leaves the borrow alone
        LD A <- [r0]
        SBC A <- [ds0]                 ; the borrow carries into the top byte
        LD [t0] <- A
        JC d16nx                       ; borrow: it did not fit, scratch is dropped

        LD A <- [t1]                   ; it fit, so the scratch pair is the rem
        LD [r1] <- A
        LD A <- [t0]
        LD [r0] <- A
        LD A <- [q1]
        OR A <- 1                      ; the quotient bit for this pass
        LD [q1] <- A

d16nx:  LD A <- [d16n]
        SUB A <- 1
        LD [d16n] <- A
        JZ d16end
        JMP d16lp
d16end: RET

.ram
; printf reads its arguments from RAM, so they are stored rather than
; pushed a byte at a time through a port.
args:   db 0, 0, 0, 0, 0, 0, 0, 0
dv0:    db 0                   ; the dividend, high byte first
dv1:    db 0
ds0:    db 0                   ; the divisor
ds1:    db 0
q0:     db 0                   ; the quotient, built where the dividend was
q1:     db 0
r0:     db 0                   ; the remainder
r1:     db 0
t0:     db 0                   ; scratch for the trial subtraction
t1:     db 0
d16n:   db 0
row:    db 0
left:   db 0
npairs: db 7
pairs:  dw 65535,7
        dw 1000,3
        dw 40000,40001
        dw 65535,65535
        dw 30000,256
        dw 12345,100
        dw 4660,0

.data
title:  db "16 / 16 divide", 0
sub:    db "SUB then SBC, and C answers", 0
hint:   db "no CMP, and none wanted", 0
ans:    db "%5u/%5u=%5u r%5u", 0
