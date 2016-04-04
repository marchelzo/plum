#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <math.h>
#include <errno.h>

#include "tags.h"
#include "value.h"
#include "vm.h"

struct value
builtin_print(value_vector *args)
{
        if (args->count != 1) {
                vm_panic("the builtin print function expects a single argument, but it was passed %zu", args->count);
        }

        if (args->items[0].type == VALUE_STRING) {
                vm_append_output(args->items[0].string, args->items[0].bytes);
        } else {
                char *s = value_show(&args->items[0]);
                vm_append_output(s, strlen(s));
                free(s);
        }


        vm_append_output("\n", 1);

        return NIL;
}

struct value
builtin_rand(value_vector *args)
{
        int low, high;

        if (args->count >= 3) {
                vm_panic("the builtin rand function expects 0, 1, or 2 arguments, but it was passed %zu", args->count);
        }

        if (args->count == 1 && args->items[0].type == VALUE_ARRAY) {
                int n = args->items[0].array->count;
                if (n == 0) {
                        return NIL;
                } else {
                        return args->items[0].array->items[rand() % n];
                }
        }

        for (int i = 0; i < args->count; ++i) {
                if (args->items[i].type != VALUE_INTEGER) {
                        vm_panic("non-integer passed as argument %d to rand", i + 1);
                }
        }

        switch (args->count) {
        case 0:  low = 0;                      high = RAND_MAX;               break;
        case 1:  low = 0;                      high = args->items[0].integer; break;
        case 2:  low = args->items[0].integer; high = args->items[1].integer; break;
        }

        return INTEGER((rand() % (high - low)) + low);

}

struct value
builtin_int(value_vector *args)
{
        struct value v = INTEGER(0), a, s, b;
        int base;

        char nbuf[64] = {0};

        char const *string = nbuf;

        switch (args->count) {
        case 0: v.integer = 0; return v;
        case 1:                goto coerce;
        case 2:                goto custom_base;
        default:               vm_panic("the builtin int function takes 0, 1, or 2 arguments, but it was passed %zu", args->count);
        }

coerce:

        a = args->items[0];
        switch (a.type) {
        case VALUE_INTEGER:                                             return a;
        case VALUE_REAL:    v.integer = a.real;                         return v;
        case VALUE_BOOLEAN: v.integer = a.boolean;                      return v;
        case VALUE_ARRAY:   v.integer = a.array->count;                 return v;
        case VALUE_OBJECT:  v.integer = object_item_count(a.object);    return v;
        case VALUE_STRING:  base = 10; memcpy(nbuf, a.string, a.bytes); goto string;
        default:                                                        return NIL;
        }

custom_base:

        s = args->items[0];
        b = args->items[1];

        if (s.type != VALUE_STRING) {
                vm_panic("non-string passed as first of two arguments to the builtin int function");
        }
        if (b.type != VALUE_INTEGER) {
                vm_panic("non-integer passed as second argument to the builtin int function");
        }
        if (b.integer < 0 || b.integer == 1 || b.integer > 36) {
                vm_panic("invalid base passed to the builtin int function: expected 0 or 2..36, but got %d", (int) b.integer);
        }

        base = b.integer;
        memcpy(nbuf, s.string, s.bytes);

        /*
         * The 0b syntax for base-2 integers is not standard C, so the strto* family of
         * functions doesn't recognize it. Thus, we must handle it specially here.
         */
        if (base == 0 && string[0] == '0' && string[1] == 'b') {
                base = 2;
                string += 2;
        }

string:

        errno = 0;

        char *end;
        intmax_t n = strtoimax(string, &end, base);

        if (errno != 0 || *end != '\0') {
                return NIL;
        }

        v.integer = n;

        return v;
}

struct value
builtin_str(value_vector *args)
{
        if (args->count > 1) {
                vm_panic("str() expects 0 or 1 arguments it was passed %zu", args->count);
        }

        if (args->count == 0) {
                return STRING_NOGC(NULL, 0);
        }

        struct value arg = args->items[0];
        if (arg.type == VALUE_STRING) {
                return arg;
        } else {
                char const *str = value_show(&arg);
                struct value result = STRING_CLONE(str, strlen(str));
                free(str);
                return result;
        }
}

struct value
builtin_bool(value_vector *args)
{
        if (args->count != 1) {
                vm_panic("the builtin bool function expects 1 argument, but it was passed %zu", args->count);
        }

        return BOOLEAN(value_truthy(&args->items[0]));
}

struct value
builtin_min(value_vector *args)
{
        if (args->count < 2) {
                vm_panic("the builtin min function expects 2 or more arguments, but it was passed %zu", args->count);
        }

        struct value min, v;
        min = args->items[0];

        for (int i = 1; i < args->count; ++i) {
                v = args->items[i];
                if (value_compare(&v, &min) < 0) {
                        min = v;
                }
        }

        return min;
}

struct value
builtin_max(value_vector *args)
{
        if (args->count < 2) {
                vm_panic("the builtin max function expects 2 or more arguments, but it was passed %zu", args->count);
        }

        struct value max, v;
        max = args->items[0];

        for (int i = 1; i < args->count; ++i) {
                v = args->items[i];
                if (value_compare(&v, &max) > 0) {
                        max = v;
                }
        }

        return max;
}

struct value
builtin_read(value_vector *args)
{
        struct string *str = value_string_alloc(256);

        if (fgets(str->data, 256, stdin) != NULL) {
                str->data[strcspn(str->data, "\n")] = '\0';
                return STRING(str->data, strlen(str->data), str);
        } else {
                return NIL;
        }
}
