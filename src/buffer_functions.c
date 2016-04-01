#include <string.h>

#include "vm.h"
#include "value.h"
#include "buffer.h"

#define ASSERT_ARGC(func, argc) \
        if (args->count != (argc)) { \
                vm_panic(func " expects " #argc " argument(s) but got %zu", args->count); \
        }

struct value
builtin_buffer_insert(value_vector *args)
{
        ASSERT_ARGC("buffer::insert()", 1);

        struct value text = args->items[0];

        if (text.type != VALUE_STRING) {
                vm_panic("non-string passed as argument to buffer::insert()");
        }

        buffer_insert_n(text.string, text.bytes);

        return NIL;
}

struct value
builtin_buffer_forward(value_vector *args)
{
        ASSERT_ARGC("buffer::forward()", 1);

        struct value distance = args->items[0];
        if (distance.type != VALUE_INTEGER) {
                vm_panic("non-integer passed to buffer::forward()");
        }

        if (distance.integer < 0) {
                vm_panic("negative distance passed to buffer::forward()");
        }

        return INTEGER(buffer_forward(distance.integer));
}

struct value
builtin_buffer_backward(value_vector *args)
{
        ASSERT_ARGC("buffer::backward()", 1);

        struct value distance = args->items[0];
        if (distance.type != VALUE_INTEGER) {
                vm_panic("non-integer passed to buffer::backward()");
        }

        if (distance.integer < 0) {
                vm_panic("negative distance passed to buffer::backward()");
        }

        return INTEGER(buffer_backward(distance.integer));
}

struct value
builtin_buffer_remove(value_vector *args)
{
        ASSERT_ARGC("buffer::remove()", 1);

        struct value amount = args->items[0];

        if (amount.type != VALUE_INTEGER) {
                vm_panic("non-integer passed to buffer::remove()");
        }

        if (amount.integer < 0) {
                vm_panic("negative amount passed to buffer::remove()");
        }

        return INTEGER(buffer_remove(amount.integer));
}

struct value
builtin_buffer_get_line(value_vector *args)
{
        if (args->count != 0 && args->count != 1) {
                vm_panic("buffer::getLine() expects 0 or 1 arguments but got %zu", args->count);
        }

        if (args->count == 0) {
                char *line = buffer_current_line();
                struct value v = STRING_CLONE(line, strlen(line));
                free(line);
                return SOME(v);
        } else {

                struct value number = args->items[0];

                if (number.type != VALUE_INTEGER) {
                        vm_panic("non-integer passed to buffer::getLine()");
                }

                if (number.integer < 0) {
                        vm_panic("negative integer passed to buffer::getLine()");
                }

                char *line = buffer_get_line(number.integer);
                if (line == NULL) {
                        return NONE;
                } else {
                        struct value v = STRING_CLONE(line, strlen(line));
                        free(line);
                        return SOME(v);
                }
        }
}

struct value
builtin_buffer_line(value_vector *args)
{
        ASSERT_ARGC("buffer::line()", 0);
        return INTEGER(buffer_line());
}

struct value
builtin_buffer_column(value_vector *args)
{
        ASSERT_ARGC("buffer::column()", 0);
        return INTEGER(buffer_column());
}

struct value
builtin_buffer_lines(value_vector *args)
{
        ASSERT_ARGC("buffer::lines()", 0);
        return INTEGER(buffer_lines());
}

struct value
builtin_buffer_grow_vertically(value_vector *args)
{
        ASSERT_ARGC("buffer::growVertically()", 1);

        struct value amount = args->items[0];

        if (amount.type != VALUE_INTEGER) {
                vm_panic("non-integer passed to buffer::growVertically()");
        }

        buffer_grow_y(amount.integer);

        return NIL;
}

struct value
builtin_buffer_grow_horizontally(value_vector *args)
{
        ASSERT_ARGC("buffer::growHorizontally()", 1);

        struct value amount = args->items[0];

        if (amount.type != VALUE_INTEGER) {
                vm_panic("non-integer passed to buffer::growHorizontally()");
        }

        buffer_grow_x(amount.integer);

        return NIL;
}

struct value
builtin_buffer_next_line(value_vector *args)
{
        if (args->count != 0 && args->count != 1) {
                vm_panic("buffer::nextLine() expects 0 or 1 arguments but got %zu", args->count);
        }

        if (args->count == 0) {
                return INTEGER(buffer_next_line(1));
        } else {
                struct value amount = args->items[0];

                if (amount.type != VALUE_INTEGER) {
                        vm_panic("non-integer passed to buffer::nextLine()");
                }

                return INTEGER(buffer_next_line(amount.integer));
        }
}

struct value
builtin_buffer_prev_line(value_vector *args)
{
        if (args->count != 0 && args->count != 1) {
                vm_panic("buffer::prevLine() expects 0 or 1 arguments but got %zu", args->count);
        }

        if (args->count == 0) {
                return INTEGER(buffer_prev_line(1));
        } else {
                struct value amount = args->items[0];

                if (amount.type != VALUE_INTEGER) {
                        vm_panic("non-integer passed to buffer::prevLine()");
                }

                return INTEGER(buffer_prev_line(amount.integer));
        }
}

struct value
builtin_buffer_scroll_up(value_vector *args)
{
        if (args->count != 0 && args->count != 1) {
                vm_panic("buffer::scrollUp() expects 0 or 1 arguments but got %zu", args->count);
        }

        if (args->count == 0) {
                return INTEGER(buffer_scroll_up(1));
        } else {
                struct value amount = args->items[0];

                if (amount.type != VALUE_INTEGER) {
                        vm_panic("non-integer passed to buffer::scrollUp()");
                }

                return INTEGER(buffer_scroll_up(amount.integer));
        }
}

struct value
builtin_buffer_scroll_down(value_vector *args)
{
        if (args->count != 0 && args->count != 1) {
                vm_panic("buffer::scrollDown() expects 0 or 1 arguments but got %zu", args->count);
        }

        if (args->count == 0) {
                return INTEGER(buffer_scroll_down(1));
        } else {
                struct value amount = args->items[0];

                if (amount.type != VALUE_INTEGER) {
                        vm_panic("non-integer passed to buffer::scrollDown()");
                }

                return INTEGER(buffer_scroll_down(amount.integer));
        }
}

struct value
builtin_buffer_move_right(value_vector *args)
{
        if (args->count != 0 && args->count != 1) {
                vm_panic("buffer::moveRight() expects 0 or 1 arguments but got %zu", args->count);
        }

        if (args->count == 0) {
                return INTEGER(buffer_right(1));
        } else {
                struct value amount = args->items[0];

                if (amount.type != VALUE_INTEGER) {
                        vm_panic("non-integer passed to buffer::moveRight()");
                }

                return INTEGER(buffer_right(amount.integer));
        }
}

struct value
builtin_buffer_move_left(value_vector *args)
{
        if (args->count != 0 && args->count != 1) {
                vm_panic("buffer::moveLeft() expects 0 or 1 arguments but got %zu", args->count);
        }

        if (args->count == 0) {
                return INTEGER(buffer_left(1));
        } else {
                struct value amount = args->items[0];

                if (amount.type != VALUE_INTEGER) {
                        vm_panic("non-integer passed to buffer::moveLeft()");
                }

                return INTEGER(buffer_left(amount.integer));
        }
}