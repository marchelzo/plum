#ifndef AST_H_INCLUDED
#define AST_H_INCLUDED

#include <stdbool.h>
#include <stdint.h>

#include "vec.h"

struct expression;
struct value;

struct environment;

struct statement {
        enum {
                STATEMENT_FOR_LOOP,
                STATEMENT_EACH_LOOP,
                STATEMENT_DEFINITION,
                STATEMENT_WHILE_LOOP,
                STATEMENT_CONDITIONAL,
                STATEMENT_RETURN,
                STATEMENT_CONTINUE,
                STATEMENT_BREAK,
                STATEMENT_FUNCTION_DEFINITION,
                STATEMENT_BLOCK,
                STATEMENT_HALT,
                STATEMENT_NULL,
                STATEMENT_EXPRESSION,
                STATEMENT_IMPORT,
        } type;
        union {
                struct expression *expression;
                struct expression *return_value;
                vec(struct statement *) statements;
                struct {
                        char *module;
                        char *as;
                } import;
                struct {
                        struct statement *init;
                        struct expression *cond;
                        struct expression *next;

                        struct statement *body;
                } for_loop;
                struct {
                        struct expression *target;
                        struct expression *array;
                        struct statement *body;
                } each;
                struct {
                        struct expression *cond;
                        struct statement *body;
                } while_loop;
                struct {
                        struct expression *cond;
                        struct statement *then_branch;
                        struct statement *else_branch;
                } conditional;
                struct {
                        char *name;
                        vec(char *) params;
                        struct statement *body;
                } function;
                struct {
                        struct expression *target;
                        struct expression *value;
                };
        };
};

struct expression {
        enum {
                EXPRESSION_FUNCTION,
                EXPRESSION_INTEGER,
                EXPRESSION_BOOLEAN,
                EXPRESSION_STRING,
                EXPRESSION_REAL,
                EXPRESSION_FUNCTION_CALL,
                EXPRESSION_MEMBER_ACCESS,
                EXPRESSION_SUBSCRIPT,
                EXPRESSION_ARRAY,
                EXPRESSION_OBJECT,
                EXPRESSION_METHOD_CALL,
                EXPRESSION_VARIABLE,
                EXPRESSION_ASSIGNMENT,

                EXPRESSION_PLUS,
                EXPRESSION_MINUS,
                EXPRESSION_STAR,
                EXPRESSION_DIV,
                EXPRESSION_PERCENT,
                EXPRESSION_AND,
                EXPRESSION_OR,
                EXPRESSION_LT,
                EXPRESSION_LEQ,
                EXPRESSION_GT,
                EXPRESSION_GEQ,
                EXPRESSION_DBL_EQ,
                EXPRESSION_NOT_EQ,

                EXPRESSION_PREFIX_MINUS,
                EXPRESSION_PREFIX_BANG,
                EXPRESSION_PREFIX_AT,
                EXPRESSION_PREFIX_INC,
                EXPRESSION_PREFIX_DEC,

                EXPRESSION_POSTFIX_INC,
                EXPRESSION_POSTFIX_DEC,

                EXPRESSION_NIL,

                EXPRESSION_IDENTIFIER_LIST,

                EXPRESSION_MODULE_ACCESS,
        } type;
        union {
                intmax_t integer;
                bool boolean;
                char *string;
                float real;
                struct expression *operand;
                vec(struct expression *) elements;
                vec(char *) identifiers;
                struct {
                        char *module;
                        char *identifier;
                };
                struct {
                        struct expression *left;
                        struct expression *right;
                };
                struct {
                        int symbol;
                        bool local;
                };
                struct {
                        struct expression *target;
                        struct expression *value;
                };
                struct {
                        vec(char *) params;
                        vec(int) param_symbols;
                        vec(int) bound_symbols;
                        struct statement *body;
                };
                struct {
                        struct expression *function;
                        vec(struct expression *) args;
                };
                struct {
                        vec(struct expression *) keys;
                        vec(struct expression *) values;
                };
                struct {
                        struct expression *container;
                        struct expression *subscript;
                };
                struct {
                        struct expression *object;
                        union {
                                char *member_name;
                                struct {
                                        char const *method_name;
                                        vec(struct expression *) method_args;
                                };
                        };
                };
        };
};

#endif
