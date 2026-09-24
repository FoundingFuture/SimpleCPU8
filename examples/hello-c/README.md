# Hello from C

The first C program. A diagonal cross of two colours and a printf line,
in one file. Build it with `simplecpu-make examples/hello-c`. That compiles
every .c file in the directory and burns hello-c.rom. Or open hello.c in
the IDE's C tab and press Compile. The GPU wrappers take one
argument per port byte, so a coordinate is its high byte then its low
byte. Halts.
