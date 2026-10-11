# BASIC libraries

A library is a `.bas` file of routines that any project can call. A
program names it with `INCLUDE` and calls a line of it with `GOSUB`:

```basic
10 INCLUDE SHAPES
20 GOSUB SHAPES:2000
```

This folder is the libraries folder of the BASIC examples. `shapes.bas`
is the one library here, and `examples/basic/shapes` is the project that
uses it. docs/guides/basic.md, chapter Libraries, is the guide.
