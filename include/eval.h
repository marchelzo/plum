#ifndef EVAL_H_INCLUDED
#define EVAL_H_INCLUDED

#include <stdarg.h>
#include <stdnoreturn.h>

#include "ast.h"
#include "value.h"
#include "environment.h"

void
eval_set_panic_hook(void (*)(void));

char const *
eval_error(void);

struct value
eval_expression(struct environment *env, struct expression const *e);

void
eval_statement(struct environment *env, struct statement const *s);

noreturn void
eval_panic(char const *fmt, ...);

#endif
