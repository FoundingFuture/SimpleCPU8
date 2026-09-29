; Circle accuracy card: angular steps are the accuracy that counts.
; One circle, radius 127, drawn two ways and split down the middle.
; RIGHT, white: 256 steps over the full turn (qsin, 65 bytes folded).
; Dots sit 3 pixels apart, so the ring shows gaps.
; LEFT, cyan: Eddie's construction. qsin2 maps 256 entries over one
; QUARTER turn; symmetry gives 1024 steps around the circle. The dots
; now sit under a pixel apart, so the ring comes out solid.
; Same byte budget per axis, four times the angular accuracy.
; Inner rings: the 64 point GPU path at scale 240, 120, 48.
        OUT GPU_COLOR, 0
        OUT GPU_CMD, CMD_CLEAR

; fine ring: 4 quadrants x 256 steps, plot only the left half
        LD A <- 0
        LD [qd] <- A
fqloop: LD A <- 0
        LD [ang] <- A
fjloop: LD A <- [qd]
        ADD A <- 1
        AND A <- 3
        LD [fqq] <- A
        JSR flook
        LD [sx] <- A
        CMP A, 128
        JNC fnext
fdraw:  LD A <- [qd]
        LD [fqq] <- A
        JSR flook
        OUTA GPU_Y
        LD A <- [sx]
        OUTA GPU_X
        OUT GPU_PIXEL, $1F
        OUT GPU_CMD, CMD_PLOT
fnext:  LD A <- [ang]
        ADD A <- 1
        LD [ang] <- A
        JNZ fjloop
fqnext: LD A <- [qd]
        ADD A <- 1
        LD [qd] <- A
        CMP A, 4
        JNZ fqloop

; coarse ring: 256 steps over the whole turn, plot only the right half
coarse: LD A <- 0
        LD [ang] <- A
cloop:  LD A <- [ang]
        ADD A <- 64
        JSR sinq
        LD [sx] <- A
        CMP A, 128
        JC cnext
        LD A <- [ang]
        JSR sinq
        OUTA GPU_Y
        LD A <- [sx]
        OUTA GPU_X
        OUT GPU_PIXEL, $FF
        OUT GPU_CMD, CMD_PLOT
cnext:  LD A <- [ang]
        ADD A <- 1
        LD [ang] <- A
        JNZ cloop

; the 64 point circle as a path, three scales
; The blob leads with its point count, so each draw says only where to read.
paths:  OUT GPU_COLOR, $1C
        OUT GPU_CMD, CMD_SET_COLOR
        OUT GPU_SCALE, 240
        OUT GPU_CMD, CMD_SET_SCALE
        JSR ringat
        OUT GPU_COLOR, $FC
        OUT GPU_CMD, CMD_SET_COLOR
        OUT GPU_SCALE, 120
        OUT GPU_CMD, CMD_SET_SCALE
        JSR ringat
        OUT GPU_COLOR, $E0
        OUT GPU_CMD, CMD_SET_COLOR
        OUT GPU_SCALE, 48
        OUT GPU_CMD, CMD_SET_SCALE
        JSR ringat
        HLT

; Put the pen in the middle and draw the ring. The pen has to be set again
; each time, because every command clears the data ports behind it.
ringat: OUT GPU_X, 128
        OUT GPU_Y, 128
        OUT GPU_CMD, CMD_MOVE_TO
        OUT GPU_CART_BANK, get_bankbyte(ring)
        OUT GPU_CART_HI, get_highbyte(ring)
        OUT GPU_CART_LO, get_lowbyte(ring)
        OUT GPU_CMD, CMD_DRAW_PATH
        RET

; --- fine lookup: sin of angle (fqq, ang) in 1024ths of a turn.
; qsin2 holds the first quadrant, 256 steps. The three folds:
; q1: sin = qsin2[256-j], q2: negate q0, q3: negate q1. The index
; 256 does not fit a byte, so j = 0 in q1 and q3 answers directly
; with the quarter point: sin = 1 or -1.
flook:  LD A <- [fqq]
        JZ f0
        CMP A, 1               ; CMP keeps the quadrant in A for the next test
        JZ f1
        CMP A, 2
        JZ f2
        LD A <- [ang]
        JZ f3z
        LD A <- 0
        SUB A <- [ang]
        JSR lk2
        XOR A <- $FF           ; negate: 256 - x is the inverse plus one
        INC A
        RET
f3z:    LD A <- 1
        RET
f2:     LD A <- [ang]
        JSR lk2
        XOR A <- $FF
        INC A
        RET
f1:     LD A <- [ang]
        JZ f1z
        LD A <- 0
        SUB A <- [ang]
        JMP lk2                ; lk2 returns for us
f1z:    LD A <- 255
        RET
f0:     LD A <- [ang]
lk2:    LD D1 <- qsin2
        LD A <- [D1+A]
        RET

; --- coarse lookup: sin of angle A in 256ths of a turn, from the
; 65 byte quarter table. Same folds, one byte of angle.
sinq:   CMP A, 128
        JC posq                ; the first half is posq itself
        SUB A <- 128
        JSR posq
        XOR A <- $FF           ; the second half is its negative
        INC A
        RET
posq:   CMP A, 65
        JC plook               ; the first quarter reads the table directly
        LD [t3] <- A
        LD A <- 128            ; the second one reads it backwards
        SUB A <- [t3]
plook:  LD D1 <- qsin
        LD A <- [D1+A]
        RET

.ram
ang:    db 0
qd:     db 0
fqq:    db 0
sx:     db 0
t3:     db 0
qsin:   db sin(0), sin(0.00390625), sin(0.0078125), sin(0.01171875), sin(0.015625), sin(0.01953125), sin(0.0234375), sin(0.02734375)
        db sin(0.03125), sin(0.03515625), sin(0.0390625), sin(0.04296875), sin(0.046875), sin(0.05078125), sin(0.0546875), sin(0.05859375)
        db sin(0.0625), sin(0.06640625), sin(0.0703125), sin(0.07421875), sin(0.078125), sin(0.08203125), sin(0.0859375), sin(0.08984375)
        db sin(0.09375), sin(0.09765625), sin(0.1015625), sin(0.10546875), sin(0.109375), sin(0.11328125), sin(0.1171875), sin(0.12109375)
        db sin(0.125), sin(0.12890625), sin(0.1328125), sin(0.13671875), sin(0.140625), sin(0.14453125), sin(0.1484375), sin(0.15234375)
        db sin(0.15625), sin(0.16015625), sin(0.1640625), sin(0.16796875), sin(0.171875), sin(0.17578125), sin(0.1796875), sin(0.18359375)
        db sin(0.1875), sin(0.19140625), sin(0.1953125), sin(0.19921875), sin(0.203125), sin(0.20703125), sin(0.2109375), sin(0.21484375)
        db sin(0.21875), sin(0.22265625), sin(0.2265625), sin(0.23046875), sin(0.234375), sin(0.23828125), sin(0.2421875), sin(0.24609375)
        db sin(0.25)
qsin2:  db sin(0), sin(0.0009765625), sin(0.001953125), sin(0.0029296875), sin(0.00390625), sin(0.0048828125), sin(0.005859375), sin(0.0068359375)
        db sin(0.0078125), sin(0.0087890625), sin(0.009765625), sin(0.0107421875), sin(0.01171875), sin(0.0126953125), sin(0.013671875), sin(0.0146484375)
        db sin(0.015625), sin(0.0166015625), sin(0.017578125), sin(0.0185546875), sin(0.01953125), sin(0.0205078125), sin(0.021484375), sin(0.0224609375)
        db sin(0.0234375), sin(0.0244140625), sin(0.025390625), sin(0.0263671875), sin(0.02734375), sin(0.0283203125), sin(0.029296875), sin(0.0302734375)
        db sin(0.03125), sin(0.0322265625), sin(0.033203125), sin(0.0341796875), sin(0.03515625), sin(0.0361328125), sin(0.037109375), sin(0.0380859375)
        db sin(0.0390625), sin(0.0400390625), sin(0.041015625), sin(0.0419921875), sin(0.04296875), sin(0.0439453125), sin(0.044921875), sin(0.0458984375)
        db sin(0.046875), sin(0.0478515625), sin(0.048828125), sin(0.0498046875), sin(0.05078125), sin(0.0517578125), sin(0.052734375), sin(0.0537109375)
        db sin(0.0546875), sin(0.0556640625), sin(0.056640625), sin(0.0576171875), sin(0.05859375), sin(0.0595703125), sin(0.060546875), sin(0.0615234375)
        db sin(0.0625), sin(0.0634765625), sin(0.064453125), sin(0.0654296875), sin(0.06640625), sin(0.0673828125), sin(0.068359375), sin(0.0693359375)
        db sin(0.0703125), sin(0.0712890625), sin(0.072265625), sin(0.0732421875), sin(0.07421875), sin(0.0751953125), sin(0.076171875), sin(0.0771484375)
        db sin(0.078125), sin(0.0791015625), sin(0.080078125), sin(0.0810546875), sin(0.08203125), sin(0.0830078125), sin(0.083984375), sin(0.0849609375)
        db sin(0.0859375), sin(0.0869140625), sin(0.087890625), sin(0.0888671875), sin(0.08984375), sin(0.0908203125), sin(0.091796875), sin(0.0927734375)
        db sin(0.09375), sin(0.0947265625), sin(0.095703125), sin(0.0966796875), sin(0.09765625), sin(0.0986328125), sin(0.099609375), sin(0.1005859375)
        db sin(0.1015625), sin(0.1025390625), sin(0.103515625), sin(0.1044921875), sin(0.10546875), sin(0.1064453125), sin(0.107421875), sin(0.1083984375)
        db sin(0.109375), sin(0.1103515625), sin(0.111328125), sin(0.1123046875), sin(0.11328125), sin(0.1142578125), sin(0.115234375), sin(0.1162109375)
        db sin(0.1171875), sin(0.1181640625), sin(0.119140625), sin(0.1201171875), sin(0.12109375), sin(0.1220703125), sin(0.123046875), sin(0.1240234375)
        db sin(0.125), sin(0.1259765625), sin(0.126953125), sin(0.1279296875), sin(0.12890625), sin(0.1298828125), sin(0.130859375), sin(0.1318359375)
        db sin(0.1328125), sin(0.1337890625), sin(0.134765625), sin(0.1357421875), sin(0.13671875), sin(0.1376953125), sin(0.138671875), sin(0.1396484375)
        db sin(0.140625), sin(0.1416015625), sin(0.142578125), sin(0.1435546875), sin(0.14453125), sin(0.1455078125), sin(0.146484375), sin(0.1474609375)
        db sin(0.1484375), sin(0.1494140625), sin(0.150390625), sin(0.1513671875), sin(0.15234375), sin(0.1533203125), sin(0.154296875), sin(0.1552734375)
        db sin(0.15625), sin(0.1572265625), sin(0.158203125), sin(0.1591796875), sin(0.16015625), sin(0.1611328125), sin(0.162109375), sin(0.1630859375)
        db sin(0.1640625), sin(0.1650390625), sin(0.166015625), sin(0.1669921875), sin(0.16796875), sin(0.1689453125), sin(0.169921875), sin(0.1708984375)
        db sin(0.171875), sin(0.1728515625), sin(0.173828125), sin(0.1748046875), sin(0.17578125), sin(0.1767578125), sin(0.177734375), sin(0.1787109375)
        db sin(0.1796875), sin(0.1806640625), sin(0.181640625), sin(0.1826171875), sin(0.18359375), sin(0.1845703125), sin(0.185546875), sin(0.1865234375)
        db sin(0.1875), sin(0.1884765625), sin(0.189453125), sin(0.1904296875), sin(0.19140625), sin(0.1923828125), sin(0.193359375), sin(0.1943359375)
        db sin(0.1953125), sin(0.1962890625), sin(0.197265625), sin(0.1982421875), sin(0.19921875), sin(0.2001953125), sin(0.201171875), sin(0.2021484375)
        db sin(0.203125), sin(0.2041015625), sin(0.205078125), sin(0.2060546875), sin(0.20703125), sin(0.2080078125), sin(0.208984375), sin(0.2099609375)
        db sin(0.2109375), sin(0.2119140625), sin(0.212890625), sin(0.2138671875), sin(0.21484375), sin(0.2158203125), sin(0.216796875), sin(0.2177734375)
        db sin(0.21875), sin(0.2197265625), sin(0.220703125), sin(0.2216796875), sin(0.22265625), sin(0.2236328125), sin(0.224609375), sin(0.2255859375)
        db sin(0.2265625), sin(0.2275390625), sin(0.228515625), sin(0.2294921875), sin(0.23046875), sin(0.2314453125), sin(0.232421875), sin(0.2333984375)
        db sin(0.234375), sin(0.2353515625), sin(0.236328125), sin(0.2373046875), sin(0.23828125), sin(0.2392578125), sin(0.240234375), sin(0.2412109375)
        db sin(0.2421875), sin(0.2431640625), sin(0.244140625), sin(0.2451171875), sin(0.24609375), sin(0.2470703125), sin(0.248046875), sin(0.2490234375)

.data
ring:   db 65        ; the blob leads with its point count
        db cos(0), sin(0), cos(0.015625), sin(0.015625), cos(0.03125), sin(0.03125), cos(0.046875), sin(0.046875)
        db cos(0.0625), sin(0.0625), cos(0.078125), sin(0.078125), cos(0.09375), sin(0.09375), cos(0.109375), sin(0.109375)
        db cos(0.125), sin(0.125), cos(0.140625), sin(0.140625), cos(0.15625), sin(0.15625), cos(0.171875), sin(0.171875)
        db cos(0.1875), sin(0.1875), cos(0.203125), sin(0.203125), cos(0.21875), sin(0.21875), cos(0.234375), sin(0.234375)
        db cos(0.25), sin(0.25), cos(0.265625), sin(0.265625), cos(0.28125), sin(0.28125), cos(0.296875), sin(0.296875)
        db cos(0.3125), sin(0.3125), cos(0.328125), sin(0.328125), cos(0.34375), sin(0.34375), cos(0.359375), sin(0.359375)
        db cos(0.375), sin(0.375), cos(0.390625), sin(0.390625), cos(0.40625), sin(0.40625), cos(0.421875), sin(0.421875)
        db cos(0.4375), sin(0.4375), cos(0.453125), sin(0.453125), cos(0.46875), sin(0.46875), cos(0.484375), sin(0.484375)
        db cos(0.5), sin(0.5), cos(0.515625), sin(0.515625), cos(0.53125), sin(0.53125), cos(0.546875), sin(0.546875)
        db cos(0.5625), sin(0.5625), cos(0.578125), sin(0.578125), cos(0.59375), sin(0.59375), cos(0.609375), sin(0.609375)
        db cos(0.625), sin(0.625), cos(0.640625), sin(0.640625), cos(0.65625), sin(0.65625), cos(0.671875), sin(0.671875)
        db cos(0.6875), sin(0.6875), cos(0.703125), sin(0.703125), cos(0.71875), sin(0.71875), cos(0.734375), sin(0.734375)
        db cos(0.75), sin(0.75), cos(0.765625), sin(0.765625), cos(0.78125), sin(0.78125), cos(0.796875), sin(0.796875)
        db cos(0.8125), sin(0.8125), cos(0.828125), sin(0.828125), cos(0.84375), sin(0.84375), cos(0.859375), sin(0.859375)
        db cos(0.875), sin(0.875), cos(0.890625), sin(0.890625), cos(0.90625), sin(0.90625), cos(0.921875), sin(0.921875)
        db cos(0.9375), sin(0.9375), cos(0.953125), sin(0.953125), cos(0.96875), sin(0.96875), cos(0.984375), sin(0.984375)
        db cos(0), sin(0)
