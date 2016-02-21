#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

bool // we use bool instead of _Noreturn void so it can be used in expressions: e.g., (foo() || panic("blah"))
panic(char const *fmt, ...)
{
        va_list ap;

        va_start(ap, fmt);
        vfprintf(stderr, fmt, ap);
        fputc('\n', stderr);
        va_end(ap);

        exit(EXIT_FAILURE);

        return false; // unreachable
}
