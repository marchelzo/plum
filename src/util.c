#include <stdio.h>
#include <stddef.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

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

bool
contains(char const *s, char c)
{
        return (c != '\0') && (strchr(s, c) != NULL);
}

char *slurp(char const *path)
{
        FILE *f = fopen(path, "r");
        if (f == NULL) {
                return NULL;
        }

        char *source = malloc(8192 * 6);
        int n = fread(source, 1, 8192, f);
        source[n] = '\0';

        return source;
}
