; Multiply two bytes into a 16-bit product, with no multiply instruction.
;
; Multiplying is shifting and adding. The multiplier's bits come out one
; at a time through SHR and ROR, low bit first, and each set bit adds the
; multiplicand into the product's high byte. ROR walks the product down a
; bit each pass, so an early add ends up low. The carry does the work: SHR
; and ROR drop the bit that leaves into C, ADD leaves its carry in C, and
; ROR brings C back in at the other end.
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
        JNZ line
done:   HLT

; mul8: [prodhi][prodlo] = [mcand] * [mplier]. Both operands survive the
; call, so the caller can still print them.
;
; Low bit first. TST reads the multiplier's bit 0. When it is set, the
; multiplicand goes into the product's high byte, and the carry out of that
; add lands in C. ROR brings C back in at the top of the high byte, and the
; high byte's bit 0 comes out into C. A second ROR walks it into the top of
; the low byte, which still holds the multiplier's unused bits. When the bit
; is clear, SHR brings in 0 instead. Eight passes and the multiplier is
; gone, the product in its place.
;
; The pass count is D2 counting up from -8. LD D2 <- D2+1 sets Z when it
; reaches zero. It also sets C, which is why the next multiplier bit is
; tested with TST rather than carried from one pass to the next in C.
mul8:   LD A <- [mplier]
        LD [prodlo] <- A
        LD A <- 0
        LD [prodhi] <- A
        LD D2 <- $FFF8                 ; -8
m8lp:   LD A <- [prodlo]
        TST A, 1                       ; the multiplier's next bit
        JZ m8no
        LD A <- [prodhi]
        ADD A <- [mcand]               ; C = the carry out
        ROR A                          ; which comes back in at the top
        JMP m8lo
m8no:   LD A <- [prodhi]
        SHR A                          ; 0 in at the top
m8lo:   LD [prodhi] <- A
        LD A <- [prodlo]
        ROR A                          ; a product bit in, a multiplier bit out
        LD [prodlo] <- A
        LD D2 <- D2+1
        JNZ m8lp
        RET

.ram
; printf reads its arguments from RAM, so they are stored rather than
; pushed a byte at a time through a port.
args:   db 0, 0, 0, 0, 0, 0, 0, 0
mcand:  db 0
mplier: db 0
prodhi: db 0                   ; big-endian and adjacent, so after the call
prodlo: db 0                   ; LD D1 <- [prodhi] picks up all 16 bits
row:    db 0
left:   db 0
npairs: db 8
pairs:  db 7,13, 12,12, 128,2, 1,255, 0,255, 100,100, 200,173, 255,255

.data
title:  db "8 x 8 multiply", 0
sub:    db "no multiply op: shift and add", 0
hint:   db "bits leave and enter through C", 0
sum:    db "%3hhu * %3hhu = %5u", 0
