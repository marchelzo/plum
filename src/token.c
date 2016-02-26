#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>

#include "token.h"
#include "alloc.h"
#include "util.h"

static char token_show_buffer[512];

static struct {
        char const *string;
        int kw_num;
} keywords[] = {
        { "break",    KEYWORD_BREAK    },
        { "continue", KEYWORD_CONTINUE },
        { "else",     KEYWORD_ELSE     },
        { "for",      KEYWORD_FOR      },
        { "function", KEYWORD_FUNCTION },
        { "if",       KEYWORD_IF       },
        { "let",      KEYWORD_LET      },
        { "return",   KEYWORD_RETURN   },
        { "while",    KEYWORD_WHILE    },
        { "true",     KEYWORD_TRUE     },
        { "false",    KEYWORD_FALSE    },
        { "nil",      KEYWORD_NIL      }
};

int
keyword_get_number(char const *s)
{
        for (size_t i = 0; i < sizeof keywords / sizeof keywords[0]; ++i) {
                if (strcmp(s, keywords[i].string) == 0) {
                        return keywords[i].kw_num;
                }
        }

        return -1;
}

char const *
keyword_show(int kw)
{
        for (size_t i = 0; i < sizeof keywords / sizeof keywords[0]; ++i) {
                if (keywords[i].kw_num == kw) {
                        return keywords[i].string;
                }
        }

        return NULL;
}

char const *
token_show_type(int type)
{
        switch (type) {
        case TOKEN_OPERATOR:   return "operator";
        case TOKEN_IDENTIFIER: return "identifier";
        case TOKEN_STRING:     return "string";
        case TOKEN_INTEGER:    return "integer";
        case TOKEN_REAL:       return "real";
        case TOKEN_KEYWORD:    return "keyword";
        default:               snprintf(token_show_buffer, 512, "token '%c'", type); return sclone(token_show_buffer);
        }
}

char const *
token_show(struct token const *t)
{
        switch (t->type) {
        case TOKEN_OPERATOR:   snprintf(token_show_buffer, 512, "operator '%s'", t->operator);             break;
        case TOKEN_IDENTIFIER: snprintf(token_show_buffer, 512, "identifier '%s'", t->identifier);         break;
        case TOKEN_STRING:     snprintf(token_show_buffer, 512, "string '%s'", t->string);                 break;
        case TOKEN_INTEGER:    snprintf(token_show_buffer, 512, "integer '%"PRIiMAX"'", t->integer);       break;
        case TOKEN_REAL:       snprintf(token_show_buffer, 512, "real '%f'", t->real);                     break;
        case TOKEN_KEYWORD:    snprintf(token_show_buffer, 512, "keyword '%s'", keyword_show(t->keyword)); break;
        case TOKEN_END:        return "end of input";
        default:               snprintf(token_show_buffer, 512, "token '%c'", t->type);                    break;
        }

        return sclone(token_show_buffer);
}
