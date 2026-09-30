/* DOUBLE: called from BASIC with CALL DOUBLE. Doubles the variable A.
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

/* TRIPLE: called from BASIC with USR(2, TRIPLE, n). The parameter and
 * the answer travel the way C passes them, so no glue is needed.
 */
int TRIPLE(int n)
{
    return n + n + n;
}

void DOUBLE(void)
{
    calls = calls + 1;
    basic_set('A', twice(basic_get('A')));
    basic_set('N', calls);
}
