#include <string.h>

#include "alloc.h"
#include "value.h"
#include "eval.h"
#include "operators.h"

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
binary_operator_addition(struct environment *env, struct expression const *left_expr, struct expression const *right_expr)
{
        struct value left = eval_expression(env, left_expr);
        struct value right = eval_expression(env, right_expr);

        if (left.type != right.type) {
                // TODO: error
        }

        switch (left.type) {
        case VALUE_INTEGER:
                return (struct value) {
                        .type = VALUE_INTEGER,
                        .integer = left.integer + right.integer
                };
        case VALUE_REAL:
                return (struct value) {
                        .type = VALUE_REAL,
                        .real = left.real + right.real
                };
        case VALUE_STRING:
                return (struct value) {
                        .type = VALUE_STRING,
                        .string = str_concat(left.string, right.string)
                };
        default:
                // TODO: error
                break;
        }
}

struct value
binary_operator_multiplication(struct environment *env, struct expression const *left_expr, struct expression const *right_expr)
{
        struct value left = eval_expression(env, left_expr);
        struct value right = eval_expression(env, right_expr);

        if (left.type != right.type) {
                // TODO: error
        }

        switch (left.type) {
        case VALUE_INTEGER:
                return (struct value) {
                        .type = VALUE_INTEGER,
                        .integer = left.integer * right.integer
                };
        case VALUE_REAL:
                return (struct value) {
                        .type = VALUE_REAL,
                        .real = left.real * right.real
                };
        default:
                // TODO: error
                break;
        }
}

struct value
binary_operator_subtraction(struct environment *env, struct expression const *left_expr, struct expression const *right_expr)
{
        struct value left = eval_expression(env, left_expr);
        struct value right = eval_expression(env, right_expr);

        if (left.type != right.type) {
                // TODO: error
        }

        switch (left.type) {
        case VALUE_INTEGER:
                return (struct value) {
                        .type = VALUE_INTEGER,
                        .integer = left.integer - right.integer
                };
        case VALUE_REAL:
                return (struct value) {
                        .type = VALUE_REAL,
                        .real = left.real - right.real
                };
        default:
                // TODO: error
                break;
        }

}

struct value
binary_operator_remainder(struct environment *env, struct expression const *left_expr, struct expression const *right_expr)
{
        struct value left = eval_expression(env, left_expr);
        struct value right = eval_expression(env, right_expr);

        if (left.type != right.type) {
                // TODO: error
        }

        switch (left.type) {
        case VALUE_INTEGER:
                return (struct value) {
                        .type = VALUE_INTEGER,
                        .integer = left.integer % right.integer
                };
        default:
                // TODO: error
                break;
        }

}

struct value
binary_operator_and(struct environment *env, struct expression const *left_expr, struct expression const *right_expr)
{
        struct value left = eval_expression(env, left_expr);
        if (left.type != VALUE_BOOLEAN) {
                // TODO: error
        }

        if (!left.boolean) {
                return left;
                        
        }

        struct value right = eval_expression(env, right_expr);
        if (right.type != VALUE_BOOLEAN) {
                // TODO: error
        }

        return right;
}

struct value
binary_operator_or(struct environment *env, struct expression const *left_expr, struct expression const *right_expr)
{
        struct value left = eval_expression(env, left_expr);
        if (left.type != VALUE_BOOLEAN) {
                // TODO: error
        }

        if (left.boolean) {
                return left;
                        
        }

        struct value right = eval_expression(env, right_expr);
        if (right.type != VALUE_BOOLEAN) {
                // TODO: error
        }

        return right;
}

struct value
binary_operator_equality(struct environment *env, struct expression const *left_expr, struct expression const *right_expr)
{
        struct value left = eval_expression(env, left_expr);
        struct value right = eval_expression(env, right_expr);

        return (struct value) {
                .type = VALUE_BOOLEAN,
                .boolean = value_test_equality(&left, &right)
        };
}

struct value
binary_operator_non_equality(struct environment *env, struct expression const *left_expr, struct expression const *right_expr)
{
        struct value left = eval_expression(env, left_expr);
        struct value right = eval_expression(env, right_expr);

        return (struct value) {
                .type = VALUE_BOOLEAN,
                .boolean = !value_test_equality(&left, &right)
        };
}

struct value
binary_operator_less_than(struct environment *env, struct expression const *left_expr, struct expression const *right_expr)
{
        struct value left = eval_expression(env, left_expr);
        struct value right = eval_expression(env, right_expr);

        if (left.type != right.type) {
                // TODO: error
        }

        switch (left.type) {
        case VALUE_INTEGER:
                return (struct value) {
                        .type = VALUE_BOOLEAN,
                        .integer = left.integer < right.integer
                };
        case VALUE_REAL:
                return (struct value) {
                        .type = VALUE_BOOLEAN,
                        .real = left.real < right.real
                };
        case VALUE_STRING:
                return (struct value) {
                        .type = VALUE_BOOLEAN,
                        .real = strcmp(left.string, right.string) < 0
                };
        default:
                // TODO: error
                break;
        }
}

struct value
binary_operator_greater_than(struct environment *env, struct expression const *left_expr, struct expression const *right_expr)
{
        struct value left = eval_expression(env, left_expr);
        struct value right = eval_expression(env, right_expr);

        if (left.type != right.type) {
                // TODO: error
        }

        switch (left.type) {
        case VALUE_INTEGER:
                return (struct value) {
                        .type = VALUE_BOOLEAN,
                        .integer = left.integer > right.integer
                };
        case VALUE_REAL:
                return (struct value) {
                        .type = VALUE_BOOLEAN,
                        .real = left.real > right.real
                };
        case VALUE_STRING:
                return (struct value) {
                        .type = VALUE_BOOLEAN,
                        .real = strcmp(left.string, right.string) > 0
                };
        default:
                // TODO: error
                break;
        }
}

struct value
binary_operator_less_than_or_equal(struct environment *env, struct expression const *left_expr, struct expression const *right_expr)
{
        struct value left = eval_expression(env, left_expr);
        struct value right = eval_expression(env, right_expr);

        if (left.type != right.type) {
                // TODO: error
        }

        switch (left.type) {
        case VALUE_INTEGER:
                return (struct value) {
                        .type = VALUE_BOOLEAN,
                        .integer = left.integer <= right.integer
                };
        case VALUE_REAL:
                return (struct value) {
                        .type = VALUE_BOOLEAN,
                        .real = left.real <= right.real
                };
        case VALUE_STRING:
                return (struct value) {
                        .type = VALUE_BOOLEAN,
                        .real = strcmp(left.string, right.string) <= 0
                };
        default:
                // TODO: error
                break;
        }
}

struct value
binary_operator_greater_than_or_equal(struct environment *env, struct expression const *left_expr, struct expression const *right_expr)
{
        struct value left = eval_expression(env, left_expr);
        struct value right = eval_expression(env, right_expr);

        if (left.type != right.type) {
                // TODO: error
        }

        switch (left.type) {
        case VALUE_INTEGER:
                return (struct value) {
                        .type = VALUE_BOOLEAN,
                        .integer = left.integer >= right.integer
                };
        case VALUE_REAL:
                return (struct value) {
                        .type = VALUE_BOOLEAN,
                        .real = left.real >= right.real
                };
        case VALUE_STRING:
                return (struct value) {
                        .type = VALUE_BOOLEAN,
                        .real = strcmp(left.string, right.string) >= 0
                };
        default:
                // TODO: error
                break;
        }
}

struct value
unary_operator_negation(struct environment *env, struct expression const *operand)
{
        struct value val = eval_expression(env, operand);

        if (val.type != VALUE_BOOLEAN) {
                eval_panic("attempt to apply the negation operator '!' to a non-boolean value");
        }

        --val.boolean;

        return val;
}
