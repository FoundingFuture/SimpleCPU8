/* The interpreter's reserved words in capitals, each between two spaces.
 * edit.c stores a typed line with these words in capitals, and the IDE's
 * sc8::basic::basicKeywords() reads the same string, so the list is kept
 * once. The file is C for simplecpu-cc and C++ for the IDE.
 *
 * A stored line holds each word as one byte, 128 plus its place in the
 * list: ABS is 128. The KW_ names below are those bytes, and a test holds
 * them to the string. A word added to the list moves every word after it,
 * which costs nothing, since SAVE writes the text. docs/design/basic-speed.md.
 */
#ifndef BASIC_KEYWORDS_H
#define BASIC_KEYWORDS_H

#define BASIC_KEYWORDS \
    " ABS AND ASC CALL CATALOG CHR$ CIRCLE CLS DATA DEEK DELETE DOKE DRAW END" \
    " FOR GOSUB GOTO HEX$ IF INK INKEY$ INPUT JMP JSR KEY LEN LET LIST LOAD" \
    " LOADFONT MID$ MOD MOVE NEW NEXT NOT OR PAD PAPER PEEK PIXEL PLOT POKE" \
    " PRINT READ REM RENUM RESTORE RETURN RND RUN SAVE SETTEXT STEP STOP STR$" \
    " THEN TO USR VAL WAIT "

/* How many words the list holds. */
#define KW_COUNT     61
#define KW_FIRST     128

#define KW_ABS       128
#define KW_AND       129
#define KW_ASC       130
#define KW_CALL      131
#define KW_CATALOG   132
#define KW_CHRS      133
#define KW_CIRCLE    134
#define KW_CLS       135
#define KW_DATA      136
#define KW_DEEK      137
#define KW_DELETE    138
#define KW_DOKE      139
#define KW_DRAW      140
#define KW_END       141
#define KW_FOR       142
#define KW_GOSUB     143
#define KW_GOTO      144
#define KW_HEXS      145
#define KW_IF        146
#define KW_INK       147
#define KW_INKEYS    148
#define KW_INPUT     149
#define KW_JMP       150
#define KW_JSR       151
#define KW_KEY       152
#define KW_LEN       153
#define KW_LET       154
#define KW_LIST      155
#define KW_LOAD      156
#define KW_LOADFONT  157
#define KW_MIDS      158
#define KW_MOD       159
#define KW_MOVE      160
#define KW_NEW       161
#define KW_NEXT      162
#define KW_NOT       163
#define KW_OR        164
#define KW_PAD       165
#define KW_PAPER     166
#define KW_PEEK      167
#define KW_PIXEL     168
#define KW_PLOT      169
#define KW_POKE      170
#define KW_PRINT     171
#define KW_READ      172
#define KW_REM       173
#define KW_RENUM     174
#define KW_RESTORE   175
#define KW_RETURN    176
#define KW_RND       177
#define KW_RUN       178
#define KW_SAVE      179
#define KW_SETTEXT   180
#define KW_STEP      181
#define KW_STOP      182
#define KW_STRS      183
#define KW_THEN      184
#define KW_TO        185
#define KW_USR       186
#define KW_VAL       187
#define KW_WAIT      188

#endif
