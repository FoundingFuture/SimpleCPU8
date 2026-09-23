# 16/16 divide

Divides one 16 bit word by another. div8 one byte wider, with SUB then SBC
and a store between them. That works because a byte store leaves C alone.
Halts.
