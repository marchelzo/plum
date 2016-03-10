#ifndef VALUE_H_INCLUDED
#define VALUE_H_INCLUDED

#include <stdbool.h>
#include <stdint.h>

#include "vec.h"
#include "ast.h"
#include "object.h"

typedef vec(struct value) value_vector;

struct value_array {
        struct value *items;
        size_t count;
        size_t capacity;

        bool mark;
        struct value_array *next;
};

struct reference {
        union {
                uintptr_t symbol;
                uintptr_t pointer;
        };
        uintptr_t offset;
};

struct ref_vector {
        bool mark;
        size_t count;
        struct ref_vector *next;
        struct reference refs[];
};

struct value {
        enum {
                VALUE_STRING,
                VALUE_INTEGER,
                VALUE_REAL,
                VALUE_BOOLEAN,
                VALUE_NIL,
                VALUE_ARRAY,
                VALUE_OBJECT,
                VALUE_FUNCTION,
                VALUE_BUILTIN_FUNCTION
        } type;
        union {
                char const *string;
                intmax_t integer;
                float real;
                bool boolean;
                struct value_array *array;
                struct object *object;
                struct value (*builtin_function)(value_vector *);
                struct {
                        vec(int) param_symbols;
                        vec(int) bound_symbols;
                        struct ref_vector *refs;
                        char *code;
                };
        };
};

unsigned long
value_hash(struct value const *val);

bool
value_test_equality(struct value const *v1, struct value const *v2);

int
value_compare(void const *v1, void const *v2);

char *
value_show(struct value const *v);

struct value_array *
value_array_new(void);

struct ref_vector *
ref_vector_new(int n);

void
value_mark(struct value *v);

void
value_array_mark(struct value_array *);

void
value_array_sweep(void);

void
value_ref_vector_sweep(void);

void
value_gc_reset(void);

#endif
