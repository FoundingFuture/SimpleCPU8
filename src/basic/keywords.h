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
    " ABS AND ASC CALL CATALOG CHR$ CIRCLE CLS DATA DEEK DELETE DOKE DRAW" \
    " END FILE FOR GOSUB GOTO HEX$ IF INCLUDE INK INKEY$ INPUT JMP JSR KEY" \
    " LEN LET LIST LOAD LOADFONT MID$ MOD MOVE NEW NEXT NOT OR PAD PAPER" \
    " PEEK PIXEL PLOT POKE PRINT READ REM RENUM RESTORE RETURN RND RUN SAVE" \
    " SETTEXT STEP STOP STR$ THEN TO USR VAL WAIT "

/* How many words the list holds. */
#define KW_COUNT     63
#define KW_FIRST     128
/* The byte after the last word's marks a number literal: the marker, the
 * value as a big-endian word, then the digits as typed. The two value
 * bytes can be anything, 0 and a colon included, so every reader of a
 * stored line steps over them by the marker. docs/design/basic-speed.md,
 * proposal 4.
 */
#define KW_LITERAL   (KW_FIRST + KW_COUNT)
/* The byte after that marks a jump target, the line GOTO, GOSUB, THEN or
 * RESTORE names: the marker, the file's index, then the line number as a
 * big-endian word. No digits follow. LIST writes the number out again,
 * with the file's name and a colon when the file is another one.
 */
#define KW_TARGET    (KW_LITERAL + 1)

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
#define KW_FILE      142
#define KW_FOR       143
#define KW_GOSUB     144
#define KW_GOTO      145
#define KW_HEXS      146
#define KW_IF        147
#define KW_INCLUDE   148
#define KW_INK       149
#define KW_INKEYS    150
#define KW_INPUT     151
#define KW_JMP       152
#define KW_JSR       153
#define KW_KEY       154
#define KW_LEN       155
#define KW_LET       156
#define KW_LIST      157
#define KW_LOAD      158
#define KW_LOADFONT  159
#define KW_MIDS      160
#define KW_MOD       161
#define KW_MOVE      162
#define KW_NEW       163
#define KW_NEXT      164
#define KW_NOT       165
#define KW_OR        166
#define KW_PAD       167
#define KW_PAPER     168
#define KW_PEEK      169
#define KW_PIXEL     170
#define KW_PLOT      171
#define KW_POKE      172
#define KW_PRINT     173
#define KW_READ      174
#define KW_REM       175
#define KW_RENUM     176
#define KW_RESTORE   177
#define KW_RETURN    178
#define KW_RND       179
#define KW_RUN       180
#define KW_SAVE      181
#define KW_SETTEXT   182
#define KW_STEP      183
#define KW_STOP      184
#define KW_STRS      185
#define KW_THEN      186
#define KW_TO        187
#define KW_USR       188
#define KW_VAL       189
#define KW_WAIT      190

#endif
