#ifndef BUILTIN_FUNCTIONS_H_INCLUDED
#define BUILTIN_FUNCTIONS_H_INCLUDED

#include "value.h"

struct value
builtin_print(value_vector *args);

struct value
builtin_rand(value_vector *args);

struct value
builtin_int(value_vector *args);

struct value
builtin_str(value_vector *args);

struct value
builtin_bool(value_vector *args);

struct value
builtin_max(value_vector *args);

struct value
builtin_min(value_vector *args);

struct value
builtin_read(value_vector *args);

#endif
