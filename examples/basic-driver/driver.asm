; A BASIC extension in assembly: the !HELLO command.
;
; BASIC's bang statement hands the rest of its line to the routine whose
; slot is DOKEd into the vector at $0000, with D1 at the text. This driver
; sits at slot $F000, apart from the interpreter, so the ROM holds the
; interpreter in one PROG segment and the driver in another. It answers
; !HELLO by writing a greeting on the text screen and returns A = 1. Any
; other word returns A = 0, and BASIC tries the storage driver next.
;
; Install it from BASIC with DOKE 0, 61440 (the vector, and slot $F000).
; The program in demo.bas does exactly that, then calls !HELLO.

.org $F000
hello:
        LD A <- [D1]+
        CMP A, 72        ; H
        JNZ pass
h0:
        LD A <- [D1]+
        CMP A, 69        ; E
        JNZ pass
h1:
        LD A <- [D1]+
        CMP A, 76        ; L
        JNZ pass
h2:
        LD A <- [D1]+
        CMP A, 76        ; L
        JNZ pass
h3:
        LD A <- [D1]+
        CMP A, 79        ; O
        JNZ pass
h4:
; Matched. Row 20 of the 42 column text screen at $FAC0 is $FAC0 + 20 * 42.
        LD D2 <- $FAC0 + 20 * 42
        LD A <- 72
        LD [D2]+ <- A
        LD A <- 69
        LD [D2]+ <- A
        LD A <- 76
        LD [D2]+ <- A
        LD A <- 76
        LD [D2]+ <- A
        LD A <- 79
        LD [D2]+ <- A
        LD A <- 32
        LD [D2]+ <- A
        LD A <- 70
        LD [D2]+ <- A
        LD A <- 82
        LD [D2]+ <- A
        LD A <- 79
        LD [D2]+ <- A
        LD A <- 77
        LD [D2]+ <- A
        LD A <- 32
        LD [D2]+ <- A
        LD A <- 65
        LD [D2]+ <- A
        LD A <- 83
        LD [D2]+ <- A
        LD A <- 83
        LD [D2]+ <- A
        LD A <- 69
        LD [D2]+ <- A
        LD A <- 77
        LD [D2]+ <- A
        LD A <- 66
        LD [D2]+ <- A
        LD A <- 76
        LD [D2]+ <- A
        LD A <- 89
        LD [D2]+ <- A
        LD A <- 1
        RET
pass:   LD A <- 0
        RET
