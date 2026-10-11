1000 REM SHAPES: drawing routines any program can call.
1010 REM A program says INCLUDE SHAPES, then GOSUB SHAPES:line.
1020 REM A library and its caller share every variable.
1030 REM SHAPES reads X, Y, W, H, R and C.
1040 REM SHAPES changes X0, X1, X2 and X3. Keep those free.
1050 REM 2000 BOX   an outline, corner X,Y, W wide, H high, colour C
1060 REM 2100 CROSS a cross through X,Y, R long each way, colour C
1070 REM 2200 DISC  a filled circle at X,Y, radius R, colour C
1080 REM 2300 FRAME a border around the whole screen, colour C
2000 INK C : MOVE X, Y
2010 DRAW X + W, Y : DRAW X + W, Y + H
2020 DRAW X, Y + H : DRAW X, Y
2030 RETURN
2100 INK C : MOVE X - R, Y : DRAW X + R, Y
2110 MOVE X, Y - R : DRAW X, Y + R
2120 RETURN
2200 INK C : MOVE X, Y : CIRCLE R, R, C
2210 RETURN
2300 X0 = X : X1 = Y : X2 = W : X3 = H
2310 X = 0 : Y = 0 : W = 255 : H = 255
2320 GOSUB 2000
2330 X = X0 : Y = X1 : W = X2 : H = X3
2340 RETURN
