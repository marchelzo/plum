#include <stddef.h>
#include <string.h>
#include <stdint.h>

#include "alloc.h"

uintmax_t
umax(uintmax_t a, uintmax_t b)
{
        return (a > b) ? a : b;
}

uintmax_t
umin(uintmax_t a, uintmax_t b)
{
        return (a < b) ? a : b;
}

intmax_t
max(intmax_t a, intmax_t b)
{
        return (a > b) ? a : b;
}

intmax_t
min(intmax_t a, intmax_t b)
{
        return (a < b) ? a : b;
}

char *
sclone(char const *s)
{
        size_t n = strlen(s);
        char *new = alloc(n + 1);
        memcpy(new, s, n + 1);
        return new;
}
