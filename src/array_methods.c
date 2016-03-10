#include <stdlib.h>
#include <string.h>

#include "value.h"
#include "log.h"
#include "vm.h"

static struct value nil = { .type = VALUE_NIL };

static struct value
array_sort(struct value *array, value_vector *args)
{
        if (args->count != 0) {
                vm_panic("the sort method on arrays expects no arguments but got %zu", args->count);
        }

        qsort(array->array->items, array->array->count, sizeof (struct value), value_compare);

        return nil;
}

static struct value
array_push(struct value *array, value_vector *args)
{
        if (args->count != 1) {
                vm_panic("the push method on arrays expects 1 argument but got %zu", args->count);
        }

        vec_push(*array->array, args->items[0]);

        return nil;
}

static struct value
array_pop(struct value *array, value_vector *args)
{
        if (args->count == 0) {
                if (array->array->count == 0) {
                        vm_panic("attempt to pop from an empty array");
                }
                return *vec_pop(*array->array);
        }

        if (args->count == 1) {
                LOG("TEST HHAH AH ");
                struct value arg = args->items[0];
                if (arg.type != VALUE_INTEGER) {
                        vm_panic("the argument to pop must be an integer");
                }
                if (arg.integer < 0) {
                        arg.integer += array->array->count;
                }
                if (arg.integer >= array->array->count) {
                        vm_panic("array index passed to pop is out of range");
                }
                struct value out;
                vec_pop_ith(*array->array, arg.integer, out);
                return out;
        }

        vm_panic("the pop method on arrays expects 0 or 1 argument(s) but got %zu", args->count);
}

static struct {
        char const *name;
        struct value (*func)(struct value *, value_vector *);
} funcs[] = {
        { .name = "push", .func = array_push },
        { .name = "pop",  .func = array_pop  },
        { .name = "sort", .func = array_sort }
};

static size_t const nfuncs = sizeof funcs / sizeof funcs[0];

struct value (*get_array_method(char const *name))(struct value *, value_vector *)
{
        for (int i = 0; i < nfuncs; ++i) {
                if (strcmp(funcs[i].name, name) == 0) {
                        return funcs[i].func;
                }
        }

        return NULL;
}
