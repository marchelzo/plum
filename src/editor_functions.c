#include <limits.h>
#include <string.h>

#include "vm.h"
#include "value.h"
#include "buffer.h"

#define ASSERT_ARGC(func, argc) \
        if (args->count != (argc)) { \
                vm_panic(func " expects " #argc " argument(s) but got %zu", args->count); \
        }

struct value
builtin_editor_insert(value_vector *args)
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
builtin_editor_forward(value_vector *args)
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
builtin_editor_backward(value_vector *args)
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
builtin_editor_remove(value_vector *args)
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
builtin_editor_get_line(value_vector *args)
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
builtin_editor_line(value_vector *args)
{
        ASSERT_ARGC("buffer::line()", 0);
        return INTEGER(buffer_line());
}

struct value
builtin_editor_column(value_vector *args)
{
        ASSERT_ARGC("buffer::column()", 0);
        return INTEGER(buffer_column());
}

struct value
builtin_editor_lines(value_vector *args)
{
        ASSERT_ARGC("buffer::lines()", 0);
        return INTEGER(buffer_lines());
}

struct value
builtin_editor_grow_vertically(value_vector *args)
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
builtin_editor_grow_horizontally(value_vector *args)
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
builtin_editor_next_line(value_vector *args)
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
builtin_editor_prev_line(value_vector *args)
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
builtin_editor_scroll_up(value_vector *args)
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
builtin_editor_scroll_down(value_vector *args)
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
builtin_editor_move_right(value_vector *args)
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
builtin_editor_move_left(value_vector *args)
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

struct value
builtin_editor_prev_window(value_vector *args)
{
        ASSERT_ARGC("editor::prevWindow()", 0);
        buffer_prev_window();
        return NIL;
}

struct value
builtin_editor_next_window(value_vector *args)
{
        ASSERT_ARGC("editor::nextWindow()", 0);
        buffer_next_window();
        return NIL;
}

struct value
builtin_editor_map_normal(value_vector *args)
{
        ASSERT_ARGC("buffer::mapNormal()", 2);

        struct value chord = args->items[0];
        struct value action = args->items[1];

        if (action.type != VALUE_FUNCTION && action.type != VALUE_BUILTIN_FUNCTION) {
                vm_panic("the second argument to buffer::mapNormal() must be a function");
        }

        if (chord.type != VALUE_ARRAY || chord.array->count == 0) {
                vm_panic("the first argument to buffer::mapNormal() must be a non-empty array of strings");
        }

        for (int i = 0; i < chord.array->count; ++i) {
                if (chord.array->items[i].type != VALUE_STRING) {
                        vm_panic("non-string in the first argument to buffer::mapNormal()");
                }
        }

        buffer_map_normal(chord.array, action);

        return NIL;
}

struct value
builtin_editor_map_insert(value_vector *args)
{
        ASSERT_ARGC("buffer::mapInsert()", 2);

        struct value chord = args->items[0];
        struct value action = args->items[1];

        if (action.type != VALUE_FUNCTION && action.type != VALUE_BUILTIN_FUNCTION) {
                vm_panic("the second argument to buffer::mapInsert() must be a function");
        }

        if (chord.type != VALUE_ARRAY || chord.array->count == 0) {
                vm_panic("the first argument to buffer::mapInsert() must be a non-empty array of strings");
        }

        for (int i = 0; i < chord.array->count; ++i) {
                if (chord.array->items[i].type != VALUE_STRING) {
                        vm_panic("non-string in the first argument to buffer::mapInsert()");
                }
        }

        buffer_map_insert(chord.array, action);

        return NIL;
}

struct value
builtin_editor_source(value_vector *args)
{
        ASSERT_ARGC("buffer::source()", 1);

        struct value path = args->items[0];
        if (path.type != VALUE_STRING) {
                vm_panic("non-string passed to buffer::source()");
        }

        buffer_source_file(path.string, path.bytes);

        return NIL;
}

struct value
builtin_editor_insert_mode(value_vector *args)
{
        ASSERT_ARGC("buffer::insertMode()", 0);
        buffer_insert_mode();
        return NIL;
}

struct value
builtin_editor_normal_mode(value_vector *args)
{
        ASSERT_ARGC("buffer::normalMode()", 0);
        buffer_normal_mode();
        return NIL;
}

struct value
builtin_editor_start_of_line(value_vector *args)
{
        ASSERT_ARGC("buffer::startOfLine()", 0);
        buffer_left(9999);
        return NIL;
}

struct value
builtin_editor_end_of_line(value_vector *args)
{
        ASSERT_ARGC("buffer::endOfLine()", 0);
        buffer_right(9999);
        return NIL;
}

struct value
builtin_editor_cut_line(value_vector *args)
{
        ASSERT_ARGC("buffer::cutLine()", 0);
        buffer_cut_line();
        return NIL;
}
