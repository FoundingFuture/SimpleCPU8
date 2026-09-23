# Text mode

The screen as characters read from data RAM at $FAC0. A pointer copies a
message there and the glyphs appear on the next frame. Halts, and the
picture stays up because the GPU reads that RAM every frame.
