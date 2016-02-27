#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <stdbool.h>
#include <inttypes.h>

#include "value.h"
#include "test.h"
#include "util.h"

static bool
equality_test_string(struct value const *v1, struct value const *v2)
{
        return strcmp(v1->string, v2->string) == 0;
}

static bool
equality_test_integer(struct value const *v1, struct value const *v2)
{
        return v1->integer == v2->integer;
}

static bool
equality_test_real(struct value const *v1, struct value const *v2)
{
        return v1->real == v2->real;
}

static bool
equality_test_boolean(struct value const *v1, struct value const *v2)
{
        return v1->boolean == v2->boolean;
}

static bool
equality_test_array(struct value const *v1, struct value const *v2)
{
        if (v1->array->count != v2->array->count) {
                return false;
        }
        
        size_t n = v1->array->count;

        for (size_t i = 0; i < n; ++i) {
                if (!value_test_equality(&v1->array->items[i], &v2->array->items[i])) {
                        return false;
                }
        }

        return true;
}

static bool
equality_test_object(struct value const *v1, struct value const *v2)
{
        return v1->object == v2->object;
}

static bool
equality_test_function(struct value const *v1, struct value const *v2)
{
        return v1->body == v2->body;
}

static bool
equality_test_nil(struct value const *v1, struct value const *v2)
{
        return true;
}

bool (*equality_tests[])(struct value const *, struct value const *) = {
        [VALUE_STRING]   = equality_test_string,
        [VALUE_ARRAY]    = equality_test_array,
        [VALUE_INTEGER]  = equality_test_integer,
        [VALUE_REAL]     = equality_test_real,
        [VALUE_OBJECT]   = equality_test_object,
        [VALUE_BOOLEAN]  = equality_test_boolean,
        [VALUE_FUNCTION] = equality_test_function,
        [VALUE_NIL]      = equality_test_nil
};

// These hash functions are based on djb's djb2 hash function, copied from http://www.cse.yorku.ca/~oz/hash.html

static unsigned long
str_hash(char const *str)
{
        unsigned long hash = 5381;
        int c;

        while ((c = *str++)) {
                hash = ((hash << 5) + hash) + c; /* hash * 33 + c */
        }

        return hash;
}

static unsigned long
int_hash(intmax_t k)
{
        unsigned long hash = 5381;
        char const *bytes = (char const *) &k;
        char c;

        for (int i = 0; i < sizeof k; ++i) {
                c = bytes[i];
                hash = ((hash << 5) + hash) + c; /* hash * 33 + c */
        }

        return hash;
}

static unsigned long
ptr_hash(void const *p)
{
        unsigned long hash = 5381;
        char const *bytes = (char const *) &p;
        char c;

        for (int i = 0; i < sizeof p; ++i) {
                c = bytes[i];
                hash = ((hash << 5) + hash) + c; /* hash * 33 + c */
        }

        return hash;
}

static unsigned long
flt_hash(float flt)
{
        unsigned long hash = 5381;
        char const *bytes = (char const *) &flt;
        char c;

        for (int i = 0; i < sizeof flt; ++i) {
                c = bytes[i];
                hash = ((hash << 5) + hash) + c; /* hash * 33 + c */
        }

        return hash;
}

static unsigned long
ary_hash(struct value const *a)
{
        unsigned long hash = 5381;

        for (int i = 0; i < a->array->count; ++i) {
                unsigned long c = value_hash(&a->array->items[i]);
                hash = ((hash << 5) + hash) + c; /* hash * 33 + c */
        }

        return hash;
}

unsigned long
value_hash(struct value const *val)
{
        switch (val->type) {
        case VALUE_NIL:       return 1;
        case VALUE_BOOLEAN:   return val->boolean ? 2 : 3;
        case VALUE_STRING:    return str_hash(val->string);
        case VALUE_INTEGER:   return int_hash(val->integer);
        case VALUE_REAL:      return flt_hash(val->real);
        case VALUE_ARRAY:     return ary_hash(val);
        case VALUE_OBJECT:    return ptr_hash(val);
        case VALUE_FUNCTION:  return ptr_hash(val);
        }

        assert(false);
}

char *
show_array(struct value const *a)
{
        size_t capacity = 1;
        size_t len = 1;
        size_t n;
        char *s = alloc(2);
        strcpy(s, "[");

#define add(str) \
                n = strlen(str); \
                if (len + n >= capacity) {\
                        capacity = 2 * (len + n) + 1; \
                        resize(s, capacity); \
                } \
                strcpy(s + len, str); \
                len += n;

        if (a->array->count >= 1) {
                char *val = value_show(&a->array->items[0]);
                add(val);
                free(val);
        }

        for (size_t i = 1; i < a->array->count; ++i) {
                char *val = value_show(&a->array->items[i]);
                add(", ");
                add(val);
                free(val);
        }

        add("]");
#undef add

        return s;
}

char *
value_show(struct value const *v)
{
        static char buffer[1024];

        switch (v->type) {
        case VALUE_INTEGER:
                snprintf(buffer, 1024, "%"PRIiMAX, v->integer);
                break;
        case VALUE_REAL:
                snprintf(buffer, 1024, "%f", v->real);
                break;
        case VALUE_STRING:
                snprintf(buffer, 1024, "'%s'", v->string);
                break;
        case VALUE_BOOLEAN:
                snprintf(buffer, 1024, "%s", v->boolean ? "true" : "false");
                break;
        case VALUE_NIL:
                snprintf(buffer, 1024, "%s", "nil");
                break;
        case VALUE_ARRAY:
                return show_array(v);
        default:
                // TODO: finish
                break;
        }

        return sclone(buffer);
}

bool
value_test_equality(struct value const *v1, struct value const *v2)
{
        if (v1->type != v2->type) {
                return false;
        }

        return equality_tests[v1->type](v1, v2);
}

TEST(hash)
{
        struct value v1 = { .type = VALUE_STRING, .string = "hello" };
        struct value v2 = { .type = VALUE_STRING, .string = "world" };

        claim(value_hash(&v1) != value_hash(&v2));
}

TEST(equality)
{
        struct value v1 = { .type = VALUE_BOOLEAN, .boolean = true };
        struct value v2 = { .type = VALUE_BOOLEAN, .boolean = false };
        claim(!value_test_equality(&v1, &v2));

        v1.type = VALUE_INTEGER;
        v2.type = VALUE_STRING;
        claim(!value_test_equality(&v1, &v2));

        v2.type = VALUE_INTEGER;

        v1.integer = v2.integer = 19;
        claim(value_test_equality(&v1, &v2));

        claim(
                value_test_equality(
                        &(struct value){ .type = VALUE_NIL },
                        &(struct value){ .type = VALUE_NIL }
                )
        );
}
