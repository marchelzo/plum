#ifndef INTERPRETER_H_INCLUDED
#define INTERPRETER_H_INCLUDED

#include <stdbool.h>

#include "value.h"

void
interpreter_init(void);

bool
interpreter_execute_source(char const *s);

bool
interpreter_eval_source(char const *s, struct value *out);

bool
interpreter_execute_file(char const *path);

char const *
interpreter_error(void);

#endif
