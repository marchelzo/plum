#ifndef TOKEN_H_INCLUDED
#define TOKEN_H_INCLUDED

#include <limits.h>
#include <stdint.h>

#include "location.h"

struct token {
        enum {
                TOKEN_KEYWORD = INT_MAX - 64,
                TOKEN_IDENTIFIER,
                TOKEN_STRING,
                TOKEN_INTEGER,
                TOKEN_REAL,
                TOKEN_END,
                TOKEN_PLUS,
                TOKEN_MINUS,
                TOKEN_STAR,
                TOKEN_DIV,
                TOKEN_PERCENT,
                TOKEN_NOT_EQ,
                TOKEN_BANG,
                TOKEN_DBL_EQ,
                TOKEN_EQ,
                TOKEN_ARROW,
                TOKEN_AND,
                TOKEN_OR,
                TOKEN_LEQ,
                TOKEN_GEQ,
                TOKEN_LT,
                TOKEN_GT,
                TOKEN_AT,
                TOKEN_INC,
                TOKEN_DEC,
        } type;
        struct location loc;
        union {
                enum {
                        KEYWORD_RETURN,
                        KEYWORD_BREAK,
                        KEYWORD_LET,
                        KEYWORD_CONTINUE,
                        KEYWORD_IF,
                        KEYWORD_FUNCTION,
                        KEYWORD_ELSE,
                        KEYWORD_FOR,
                        KEYWORD_IN,
                        KEYWORD_WHILE,
                        KEYWORD_TRUE,
                        KEYWORD_FALSE,
                        KEYWORD_NIL,
                        KEYWORD_IMPORT,
                } keyword;
                char *identifier;
                char *operator;
                char *string;
                intmax_t integer;
                float real;
        };
};

char const *
token_show(struct token const *t);

char const *
token_show_type(int type);

char const *
keyword_show(int t);

int
keyword_get_number(char const *s);

int
operator_get_token_type(char const *s);

#endif
