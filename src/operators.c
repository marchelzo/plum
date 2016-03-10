#include <string.h>

#include "alloc.h"
#include "value.h"
#include "operators.h"
#include "object.h"
#include "vm.h"

static char *
str_concat(char const *s1, char const *s2)
{
        size_t n1 = strlen(s1);
        size_t n2 = strlen(s2);
        char *new = alloc(n1 + n2 + 1);

        memcpy(new, s1, n1);
        memcpy(new + n1, s2, n2);
        new[n1 + n2] = '\0';

        return new;
}

struct value
binary_operator_addition(struct value const *left, struct value const *right)
{

        if (left->type != right->type) {
                vm_panic("the operands to + must have the same type");
        }

        switch (left->type) {
        case VALUE_INTEGER:
                return (struct value) {
                        .type = VALUE_INTEGER,
                        .integer = left->integer + right->integer
                };
        case VALUE_REAL:
                return (struct value) {
                        .type = VALUE_REAL,
                        .real = left->real + right->real
                };
        case VALUE_STRING:
                return (struct value) {
                        .type = VALUE_STRING,
                        .string = str_concat(left->string, right->string)
                };
        default:
                vm_panic("+ applied to operands of invalid type");
                break;
        }
}

struct value
binary_operator_multiplication(struct value const *left, struct value const *right)
{

        if (left->type != right->type) {
                vm_panic("the operands to * must have the same type");
        }

        switch (left->type) {
        case VALUE_INTEGER:
                return (struct value) {
                        .type = VALUE_INTEGER,
                        .integer = left->integer * right->integer
                };
        case VALUE_REAL:
                return (struct value) {
                        .type = VALUE_REAL,
                        .real = left->real * right->real
                };
        default:
                vm_panic("* applied to operands of invalid type");
                break;
        }
}

struct value
binary_operator_division(struct value const *left, struct value const *right)
{

        if (left->type != right->type) {
                vm_panic("the operands to / must have the same type");
        }

        switch (left->type) {
        case VALUE_INTEGER:
                return (struct value) {
                        .type = VALUE_INTEGER,
                        .integer = left->integer / right->integer
                };
        case VALUE_REAL:
                return (struct value) {
                        .type = VALUE_REAL,
                        .real = left->real / right->real
                };
        default:
                vm_panic("/ applied to operands of invalid type");
                break;
        }
}

struct value
binary_operator_subtraction(struct value const *left, struct value const *right)
{

        if (left->type != right->type) {
                vm_panic("the operands to - must have the same type");
        }

        switch (left->type) {
        case VALUE_INTEGER:
                return (struct value) {
                        .type = VALUE_INTEGER,
                        .integer = left->integer - right->integer
                };
        case VALUE_REAL:
                return (struct value) {
                        .type = VALUE_REAL,
                        .real = left->real - right->real
                };
        default:
                vm_panic("- applied to operands of invalid type");
                break;
        }

}

struct value
binary_operator_remainder(struct value const *left, struct value const *right)
{

        if (left->type != right->type) {
                vm_panic("the operands to - must have the same type");
        }

        switch (left->type) {
        case VALUE_INTEGER:
                return (struct value) {
                        .type = VALUE_INTEGER,
                        .integer = left->integer % right->integer
                };
        default:
                vm_panic("the operands to - must be integers");
                break;
        }

}

struct value
binary_operator_and(struct value const *left, struct value const *right)
{
        if (left->type != VALUE_BOOLEAN) {
                // TODO: error
        }

        if (!left->boolean) {
                return *left;
                        
        }

        if (right->type != VALUE_BOOLEAN) {
                // TODO: error
        }

        return *right;
}

struct value
binary_operator_or(struct value const *left, struct value const *right)
{
        if (left->type != VALUE_BOOLEAN) {
                // TODO: error
        }

        if (left->boolean) {
                return *left;
                        
        }

        if (right->type != VALUE_BOOLEAN) {
                // TODO: error
        }

        return *right;
}

struct value
binary_operator_equality(struct value const *left, struct value const *right)
{

        return (struct value) {
                .type = VALUE_BOOLEAN,
                .boolean = value_test_equality(left, right)
        };
}

struct value
binary_operator_non_equality(struct value const *left, struct value const *right)
{

        return (struct value) {
                .type = VALUE_BOOLEAN,
                .boolean = !value_test_equality(left, right)
        };
}

struct value
binary_operator_less_than(struct value const *left, struct value const *right)
{

        if (left->type != right->type) {
                vm_panic("< applied to operands of different types");
        }

        switch (left->type) {
        case VALUE_INTEGER:
                return (struct value) {
                        .type = VALUE_BOOLEAN,
                        .boolean = left->integer < right->integer
                };
        case VALUE_REAL:
                return (struct value) {
                        .type = VALUE_BOOLEAN,
                        .boolean = left->real < right->real
                };
        case VALUE_STRING:
                return (struct value) {
                        .type = VALUE_BOOLEAN,
                        .boolean = strcmp(left->string, right->string) < 0
                };
        default:
                vm_panic("< applied to operands of invlalid type");
                break;
        }
}

struct value
binary_operator_greater_than(struct value const *left, struct value const *right)
{

        if (left->type != right->type) {
                vm_panic("> applied to operands of different types");
        }

        switch (left->type) {
        case VALUE_INTEGER:
                return (struct value) {
                        .type = VALUE_BOOLEAN,
                        .boolean = left->integer > right->integer
                };
        case VALUE_REAL:
                return (struct value) {
                        .type = VALUE_BOOLEAN,
                        .boolean = left->real > right->real
                };
        case VALUE_STRING:
                return (struct value) {
                        .type = VALUE_BOOLEAN,
                        .boolean = strcmp(left->string, right->string) > 0
                };
        default:
                vm_panic("> applied to operands of invalid type");
                break;
        }
}

struct value
binary_operator_less_than_or_equal(struct value const *left, struct value const *right)
{

        if (left->type != right->type) {
                vm_panic("<= applied to operands of different types");
        }

        switch (left->type) {
        case VALUE_INTEGER:
                return (struct value) {
                        .type = VALUE_BOOLEAN,
                        .boolean = left->integer <= right->integer
                };
        case VALUE_REAL:
                return (struct value) {
                        .type = VALUE_BOOLEAN,
                        .boolean = left->real <= right->real
                };
        case VALUE_STRING:
                return (struct value) {
                        .type = VALUE_BOOLEAN,
                        .boolean = strcmp(left->string, right->string) <= 0
                };
        default:
                vm_panic("<= applied to operands of invalid type");
                break;
        }
}

struct value
binary_operator_greater_than_or_equal(struct value const *left, struct value const *right)
{
        if (left->type != right->type) {
                vm_panic(">= applied to operands of different types");
        }

        switch (left->type) {
        case VALUE_INTEGER:
                return (struct value) {
                        .type = VALUE_BOOLEAN,
                        .boolean = left->integer >= right->integer
                };
        case VALUE_REAL:
                return (struct value) {
                        .type = VALUE_BOOLEAN,
                        .boolean = left->real >= right->real
                };
        case VALUE_STRING:
                return (struct value) {
                        .type = VALUE_BOOLEAN,
                        .boolean = strcmp(left->string, right->string) >= 0
                };
        default:
                vm_panic(">= applied to operands of invalid type");
                break;
        }
}

struct value
unary_operator_not(struct value const *operand)
{
        if (operand->type != VALUE_BOOLEAN) {
                vm_panic("the operand to ! must be a boolean");
        }

        return (struct value) {
                .type = VALUE_BOOLEAN,
                .boolean = !operand->boolean
        };
}

struct value
unary_operator_negate(struct value const *operand)
{
        if (operand->type == VALUE_INTEGER) {
                return (struct value) {
                        .type = VALUE_INTEGER,
                        .integer = -operand->integer
                };
        } else if (operand->type == VALUE_REAL) {
                return (struct value) {
                        .type = VALUE_REAL,
                        .real = -operand->real
                };
        } else {
                vm_panic("the operand to unary - must be numeric");
        }
}

struct value
unary_operator_keys(struct value const *operand)
{
        if (operand->type != VALUE_OBJECT) {
                vm_panic("the operand to @ must be an object");
        }

        return object_keys_array(operand->object);
}
