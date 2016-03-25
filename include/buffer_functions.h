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

struct value
builtin_buffer_get_line(value_vector *args);

struct value
builtin_buffer_line(value_vector *args);

struct value
builtin_buffer_column(value_vector *args);

struct value
builtin_buffer_lines(value_vector *args);

struct value
builtin_buffer_grow_horizontally(value_vector *args);

struct value
builtin_buffer_grow_vertically(value_vector *args);

struct value
builtin_buffer_next_line(value_vector *args);

struct value
builtin_buffer_prev_line(value_vector *args);

struct value
builtin_buffer_scroll_down(value_vector *args);

struct value
builtin_buffer_scroll_up(value_vector *args);

struct value
builtin_buffer_move_right(value_vector *args);

struct value
builtin_buffer_move_left(value_vector *args);

#endif
