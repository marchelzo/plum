#include <stdio.h>

#include "value.h"
#include "vm.h"

struct value
builtin_print(value_vector *args)
{
        if (args->count != 1) {
                vm_panic("the builtin print function expects a single argument, but it was passed %zu", args->count);
        }

        if (args->items[0].type == VALUE_STRING) {
                puts(args->items[0].string);
        } else {
                puts(value_show(&args->items[0]));
        }

        return (struct value){ .type = VALUE_NIL };
}
