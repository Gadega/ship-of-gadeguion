# V46 PS3 Newlib Assert Fix

PS3 newlib declares and implements `__assert(file, line, expression)`. Shipwright's
portable function list declared a different desktop signature,
`__assert(expression, file, line)`, before `z_std_dma.c` included `<assert.h>`.
That produced a conflicting declaration.

The generic Shipwright declaration is now excluded when `__PS3__` is defined.
PS3 code therefore uses the SDK header, macro, signature, and implementation.
No assertion is disabled and no stub is introduced.
