/* DOUBLE: called from BASIC with JSR DOUBLE. Doubles the variable A.
 *
 * Named the way BASIC calls it. The count of calls lives in a global, to
 * show that a routine BASIC reaches keeps its state between calls.
 */
#include <basicvars.h>

int calls;

static int twice(int v)
{
    return v + v;
}

void DOUBLE(void)
{
    calls = calls + 1;
    basic_set('A', twice(basic_get('A')));
    basic_set('N', calls);
}
