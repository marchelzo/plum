#ifndef UTIL_H_INCLUDED
#define UTIL_H_INCLUDED

#include <stddef.h>
#include <stdint.h>

#define writeint(mem, val) \
        (memcpy(mem, &(int){val}, sizeof (int)), \
        (mem + sizeof (int))) \

#define readint(mem, out) \
        (memcpy(out, mem, sizeof (int)), \
        (mem + sizeof (int))) \

uintmax_t
umax(uintmax_t a, uintmax_t b);

uintmax_t
umin(uintmax_t a, uintmax_t b);

intmax_t
max(intmax_t a, intmax_t b);

intmax_t
min(intmax_t a, intmax_t b);

char *
sclone(char const *s);

#endif
