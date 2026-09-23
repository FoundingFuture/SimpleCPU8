# Mandelbrot on the coprocessor

Draws the Mandelbrot set with complex arithmetic on the coprocessor. Every
pass is a complex multiply and add of 64 bit floats, which the CPU cannot do.
Five passes refine the picture from 16 points across to 256. Each plane
point is computed once across all five. Halts after about 142M instructions.
