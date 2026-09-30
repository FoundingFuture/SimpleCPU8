; Random pixels forever. Read three random bytes from the GPU random
; port and use them as x, y, and color. Plot one pixel, then repeat.
; The screen fills with colored confetti. The random port needs no
; command: reading it returns the next value.

        OUT GPU_X_HI, 0                ; keep x and y in 0..255
        OUT GPU_Y_HI, 0
loop:
        IN GPU_RAND -> A              ; x
        OUTA GPU_X
        IN GPU_RAND -> A              ; y
        OUTA GPU_Y
        IN GPU_RAND -> A             ; color
        OUTA GPU_PIXEL
        OUT GPU_CMD, CMD_PLOT
        JMP loop
