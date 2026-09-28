/* The interpreter's reserved words in capitals, each between two spaces.
 * edit.c stores a typed line with these words in capitals, and the IDE's
 * sc8::basic::basicKeywords() reads the same string, so the list is kept
 * once. The file is C for simplecpu-cc and C++ for the IDE.
 */
#ifndef BASIC_KEYWORDS_H
#define BASIC_KEYWORDS_H

#define BASIC_KEYWORDS \
    " ABS AND ASC CALL CATALOG CHR$ CIRCLE CLS DATA DEEK DELETE DOKE DRAW END" \
    " FOR GOSUB GOTO HEX$ IF INK INKEY$ INPUT JMP JSR KEY LEN LET LIST LOAD" \
    " MID$ MOD MOVE NEW NEXT NOT OR PAD PAPER PEEK PIXEL PLOT POKE PRINT" \
    " READ REM RENUM RESTORE RETURN RND RUN SAVE STEP STOP STR$ THEN TO USR VAL WAIT "

#endif
