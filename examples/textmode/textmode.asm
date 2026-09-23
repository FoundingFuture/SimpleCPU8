; Text mode: the screen is characters read straight from data RAM.
; The grid is 42 by 32, which is 1344 bytes, so mapping it at $FAC0 puts the
; last cell at $FFFF. Switch the video mode to text, then copy a message
; there with a pointer. Every store into that region shows up as a glyph on
; the very next frame.

        OUT GPU_TEXT_COLOR, $1E        ; bright cyan text
        OUT GPU_TEXT_BG, 1             ; dark blue background
        OUT GPU_CMD, CMD_TEXT_STYLE

; One command sets the mode and the framebuffer together, so text mode with
; nothing mapped is not a state this machine can reach.
        OUT GPU_ADDR_HI, $FA           ; base address = $FAC0
        OUT GPU_ADDR_LO, $C0
        OUT GPU_CMD, CMD_SET_TEXTMODE

; Copy the message into the screen buffer. D2 walks the source in low
; RAM, D1 walks the screen in high RAM. A byte load sets Z, so the
; zero terminator ends the loop with no separate compare.
        LD D1 <- $FAC0
        LD D2 <- msg
copy:   LD A <- [D2]+
        JZ done
        LD [D1]+ <- A
        JMP copy

; The GPU keeps composing the screen from RAM after the CPU stops, so
; there is nothing left to run. HLT costs no cycles, a spin loop burns
; the machine for the same picture.
done:   HLT

.ram
msg: db "HELLO FROM RAM.  Text mode reads", 0
