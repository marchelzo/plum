#ifndef OPERATORS_H_INCLUDED
#define OPERATORS_H_INCLUDED

#include "value.h"

struct value
binary_operator_addition(struct environment *env, struct expression const *left, struct expression const *right);

struct value
binary_operator_multiplication(struct environment *env, struct expression const *left, struct expression const *right);

struct value
binary_operator_subtraction(struct environment *env, struct expression const *left, struct expression const *right);

struct value
binary_operator_remainder(struct environment *env, struct expression const *left, struct expression const *right);

struct value
binary_operator_and(struct environment *env, struct expression const *left, struct expression const *right);

struct value
binary_operator_or(struct environment *env, struct expression const *left, struct expression const *right);

struct value
binary_operator_equality(struct environment *env, struct expression const *left, struct expression const *right);

struct value
binary_operator_non_equality(struct environment *env, struct expression const *left, struct expression const *right);

struct value
binary_operator_less_than(struct environment *env, struct expression const *left, struct expression const *right);

struct value
binary_operator_greater_than(struct environment *env, struct expression const *left, struct expression const *right);

struct value
binary_operator_less_than_or_equal(struct environment *env, struct expression const *left, struct expression const *right);

struct value
binary_operator_greater_than_or_equal(struct environment *env, struct expression const *left, struct expression const *right);

struct value
unary_operator_negation(struct environment *env, struct expression const *operand);

#endif
