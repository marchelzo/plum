#ifndef AST_H_INCLUDED
#define AST_H_INCLUDED

#include <stdbool.h>
#include <stdint.h>

#include "vec.h"

struct expression;
struct value;

struct environment;

struct lvalue {
        enum {
                LVALUE_NAME,
                LVALUE_ARRAY,
                LVALUE_SUBSCRIPT,
                LVALUE_MEMBER
        } type;
        union {
                char const *name;
                vec(struct lvalue *) lvalues;
                struct {
                        struct expression *container;
                        struct expression *subscript;
                };
                struct {
                        struct expression *object;
                        char const *member_name;
                };
        };
};

struct statement {
        enum {
                STATEMENT_FOR_LOOP,
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
                STATEMENT_EXPRESSION
        } type;
        union {
                struct expression *expression;
                struct expression *return_value;
                vec(struct statement *) statements;
                struct {
                        // for-loop controlling expressions
                        struct statement *init;
                        struct expression *cond;
                        struct expression *next;

                        struct statement *body;
                } for_loop;
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
                        struct lvalue *target;
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
                EXPRESSION_UNOP,
                EXPRESSION_BINOP,
                EXPRESSION_NIL
        } type;
        union {
                intmax_t integer;
                bool boolean;
                char *string;
                float real;
                char *identifier;
                vec(struct expression *) elements;
                struct {
                        struct lvalue *target;
                        struct expression *value;
                };
                struct {
                        vec(char *) params;
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
                        struct expression *(*unop)(struct expression *);
                        struct expression *operand;
                };
                struct {
                        struct value (*binop)(struct environment *, struct expression const *, struct expression const *);
                        struct expression *left;
                        struct expression *right;
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
