#ifndef VALUE_H_INCLUDED
#define VALUE_H_INCLUDED

#include <stdbool.h>
#include <stdint.h>

#include "vec.h"
#include "ast.h"
#include "object.h"
#include "tags.h"

#define INTEGER(k)    ((struct value){ .type = VALUE_INTEGER,          .integer          = (k), .tags = 0 })
#define REAL(f)       ((struct value){ .type = VALUE_REAL,             .real             = (f), .tags = 0 })
#define BOOLEAN(b)    ((struct value){ .type = VALUE_BOOLEAN,          .boolean          = (b), .tags = 0 })
#define ARRAY(a)      ((struct value){ .type = VALUE_ARRAY,            .array            = (a), .tags = 0 })
#define OBJECT(o)     ((struct value){ .type = VALUE_OBJECT,           .object           = (o), .tags = 0 })
#define REGEX(r)      ((struct value){ .type = VALUE_REGEX,            .regex            = (r), .tags = 0 })
#define FUNCTION()    ((struct value){ .type = VALUE_FUNCTION,                                  .tags = 0 })
#define BUILTIN(f)    ((struct value){ .type = VALUE_BUILTIN_FUNCTION, .builtin_function = (f), .tags = 0 })
#define TAG(t)        ((struct value){ .type = VALUE_TAG,              .tag              = (t), .tags = 0 })
#define NIL           ((struct value){ .type = VALUE_NIL,                                       .tags = 0 })

#define STRING(s)     ((struct value){ .type = VALUE_STRING, .string = (s), .bytes = strlen((s)), .tags = 0 })
#define STRINGN(s, n) ((struct value){ .type = VALUE_STRING, .string = (s), .bytes = (n),         .tags = 0 })

#define CALLABLE(v) (((v).type & (VALUE_FUNCTION | VALUE_BUILTIN_FUNCTION | VALUE_REGEX | VALUE_TAG)) != 0)

#define NONE          TAG(1)

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
                VALUE_STRING           = 0,
                VALUE_REGEX            = 1,
                VALUE_INTEGER          = 2,
                VALUE_REAL             = 4,
                VALUE_BOOLEAN          = 8,
                VALUE_NIL              = 16,
                VALUE_ARRAY            = 32,
                VALUE_OBJECT           = 64,
                VALUE_FUNCTION         = 128,
                VALUE_BUILTIN_FUNCTION = 256,
                VALUE_TAG              = 512,
                VALUE_TAGGED           = 1024,
        } type;
        int tags;
        union {
                short tag;
                intmax_t integer;
                float real;
                bool boolean;
                struct value_array *array;
                struct object *object;
                struct value (*builtin_function)(value_vector *);
                struct {
                        char const *string;
                        size_t bytes;
                };
                struct {
                        pcre *regex;
                        pcre_extra *extra;
                        char const *pattern;
                };
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

bool
value_truthy(struct value const *v);

bool
value_apply_predicate(struct value *p, struct value *v);

struct value
value_apply_callable(struct value const *f, struct value const *v);

char *
value_show(struct value const *v);

struct value_array *
value_array_new(void);

struct value_array *
value_array_clone(struct value_array const *);

void
value_array_extend(struct value_array *, struct value_array const *);

struct ref_vector *
ref_vector_new(int n);

void
value_mark(struct value *v);

void
value_array_sweep(void);

void
value_ref_vector_sweep(void);

void
value_gc_reset(void);

inline static struct value
SOME(struct value v)
{
        struct value s = v;
        s.tags = tags_push(s.tags, 2);
        return s;
}

#endif
