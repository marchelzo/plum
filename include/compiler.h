#ifndef COMPILE_H_INCLUDED
#define COMPILE_H_INCLUDED

char const *
compiler_error(void);

void
compiler_init(void);

void
compiler_introduce_symbol(char const *);

char *
compiler_compile_source(char const *source, int *symbols);

#endif
