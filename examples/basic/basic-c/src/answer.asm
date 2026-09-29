; ANSWER: called from BASIC with CALL ANSWER. Comes back with 42 in A,
; which BASIC parks at $04, so PEEK(4) reads it.
ANSWER: LD A <- 42
        RET
