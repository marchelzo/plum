#ifndef VALUE_H_INCLUDED
#define VALUE_H_INCLUDED

#include <stdbool.h>
#include <stdint.h>

#include "vec.h"
#include "ast.h"
#include "object.h"

typedef vec(struct value) value_vector;

struct environment;

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
                char *string;
                intmax_t integer;
                float real;
                bool boolean;
                vec(struct value) *array;
                struct object *object;
                struct value (*builtin_function)(value_vector *);
                struct {
                        vec(char *) params;
                        struct statement *body;
                        struct environment *env;
                };
        };
};

unsigned long
value_hash(struct value const *val);

bool
value_test_equality(struct value const *v1, struct value const *v2);

char *
value_show(struct value const *v);

#endif
