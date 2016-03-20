#include "vm.h"
#include "value.h"
#include "buffer.h"

struct value
builtin_buffer_insert(value_vector *args)
{
        if (args->count != 1) {
                vm_panic("buffer::insert() expects 1 argument but got %zu", args->count);
        }

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
        return NIL;
}

struct value
builtin_buffer_backward(value_vector *args)
{
        return NIL;
}

struct value
builtin_buffer_remove(value_vector *args)
{
        return NIL;
}
