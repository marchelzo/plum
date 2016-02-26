#include "value.h"

struct value
builtin_print(value_vector *args)
{
        if (args->count != 1) {
                // TODO: error
        }

        if (args->items[0].type == VALUE_STRING) {
                puts(args->items[0].string);
        } else {
                puts(value_show(&args->items[0]));
        }

        return (struct value){ .type = VALUE_NIL };
}
