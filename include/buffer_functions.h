#ifndef BUFFER_FUNCTIONS_H_INCLUDED
#define BUFFER_FUNCTIONS_H_INCLUDED

#include "value.h"

struct value
builtin_buffer_insert(value_vector *args);

struct value
builtin_buffer_forward(value_vector *args);

struct value
builtin_buffer_backward(value_vector *args);

struct value
builtin_buffer_remove(value_vector *args);

#endif
