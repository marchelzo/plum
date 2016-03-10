#include <ctype.h>
#include <setjmp.h>
#include <stdarg.h>
#include <string.h>
#include <stdbool.h>
#include <assert.h>
#include <errno.h>
#include <math.h>

#include "vec.h"
#include "token.h"
#include "test.h"
#include "util.h"

enum {
        MAX_OP_LEN   = 8,
        MAX_ERR_LEN  = 2048
};

static char const *chars;
static vec(struct token) tokens;
static struct location startloc;
static struct location loc;
static jmp_buf jb;

static char errbuf[MAX_ERR_LEN + 1];

static char const *opchars = "=?<~|!@#$%^&*-+>";

static void
error(char const *fmt, ...)
{
        va_list ap;
        va_start(ap, fmt);

        char *err = errbuf;
        err += sprintf(err, "Error at %zu:%zu: ", loc.line + 1, loc.col + 1);
        err[vsnprintf(err, MAX_ERR_LEN, fmt, ap)] = '\0';

        va_end(ap);

        vec_empty(tokens);

        longjmp(jb, 1);
}

static struct token *
newtoken(int type)
{
        struct token t = {
                .type = type,
                .loc  = startloc,
        };

        return vec_push(tokens, t);
}

static char
nextchar(void)
{
        char c = *chars;

        if (c == '\n') {
                loc.line += 1;
                loc.col = 0;
        } else {
                loc.col += 1;
        }

        chars += 1;

        return c;
}

inline static void
skipspace(void)
{
        int n = 0;
        while (isspace(chars[n])) {
                n += 1;
        }

        if (chars[n] == '\0') {
                chars += n;
        } else {
                while (n --> 0) {
                        nextchar();
                }
        }
}

// lexes an identifier or a keyword
static void
lexword(void)
{
        vec(char) word;
        vec_init(word);

        while (isalnum(*chars) || *chars == '_') {
                vec_push(word, nextchar());
        }

        vec_push(word, '\0');

        int keyword;
        if (keyword = keyword_get_number(word.items), keyword != -1) {
                newtoken(TOKEN_KEYWORD)->keyword = keyword;
        } else {
                newtoken(TOKEN_IDENTIFIER)->identifier = word.items;
        }
}

static void
lexstr(void)
{
        vec(char) str;
        vec_init(str);

        char quote = nextchar();

        while (*chars != quote) {
                switch (*chars) {
                case '\0': goto unterminated;
                case '\\':
                           nextchar();
                           if (*chars == '\0') goto unterminated;
                           // fallthrough
                default:
                           vec_push(str, nextchar());
                }
        }

        assert(nextchar() == quote);

        vec_push(str, '\0');

        newtoken(TOKEN_STRING)->string = str.items;

        return;

unterminated:
        error("unterminated string literal");
}

static void
lexnum(void)
{
        char *end;
        errno = 0;
        intmax_t integer = strtoull(chars, &end, 0);

        if (errno != 0) {
                error("invalid numeric literal: %s", strerror(errno));
        }

        if (*end == '.') {
                errno = 0;
                float real = strtof(chars, &end);

                if (errno != 0) {
                        error("invalid numeric literal: %s", strerror(errno));
                }

                if (isalnum(*end)) {
                        error("invalid trailing character after numeric literal: %c", *end);
                }

                newtoken(TOKEN_REAL)->real = real;
        } else {
                if (isalnum(*end)) {
                        error("invalid trailing character after numeric literal: %c", *end);
                }

                newtoken(TOKEN_INTEGER)->integer = integer;
        }

        while (chars != end) {
                nextchar();
        }
}

static void
lexop(void)
{
        char op[MAX_OP_LEN + 1] = {0};
        size_t i = 0;
        
        while (contains(opchars, *chars)) {
                if (i == MAX_OP_LEN) {
                        error("operator contains too many characters: '%s...'", op);
                } else {
                        op[i++] = nextchar();
                }
        }

        int toktype = operator_get_token_type(op);
        if (toktype == -1) {
                error("invalid operator encountered: '%s'", op);
        }

        newtoken(toktype);
}

static void
dolex(void)
{
        while (*chars != '\0') {
                startloc = loc;
                if (contains(opchars, *chars))
                        lexop();
                else if (isalpha(*chars) || *chars == '_')
                        lexword();
                else if (isdigit(*chars))
                        lexnum();
                else if (contains("'\"", *chars))
                        lexstr();
                else if (isspace(*chars))
                        skipspace();
                else
                        newtoken(nextchar());
        }
}

char const *
lex_error(void)
{
        return errbuf;
}

struct token *
lex(char const *s)
{
        chars = s;
        vec_init(tokens);
        loc = (struct location) { 0, 0 };

        if (setjmp(jb) != 0) {
                return NULL;
        }

        dolex();

        struct token end = {
                .type = TOKEN_END,
                .loc = loc
        };
        vec_push(tokens, end);

        return tokens.items;
}

TEST(bigop)
{
        claim(lex("\n\n+++++++++++++++++") == NULL);
}

TEST(op)
{
        struct token *op = lex("\n\n&&");
        claim(op->type == TOKEN_AND);
}

TEST(id)
{
        struct token *id = lex("_abc123");
        claim(id != NULL);
        claim(id->type == TOKEN_IDENTIFIER);
        claim(strcmp(id->identifier, "_abc123") == 0);
}

TEST(str)
{
        struct token *str = lex("'test'");
        claim(str != NULL);
        claim(str->type == TOKEN_STRING);
        claim(strcmp(str->string, "test") == 0);

        str = lex("'test\\'ing'");
        claim(str != NULL);
        claim(str->type == TOKEN_STRING);
        claim(strcmp(str->string, "test'ing") == 0);

        str = lex("\"test'ing\"");
        claim(str != NULL);
        claim(str->type == TOKEN_STRING);
        claim(strcmp(str->string, "test'ing") == 0);
}

TEST(integer)
{
        struct token *integer = lex("010");
        claim(integer != NULL);
        claim(integer->type == TOKEN_INTEGER);
        claim(integer->integer == 010);

        integer = lex("0xFF");
        claim(integer != NULL);
        claim(integer->type == TOKEN_INTEGER);
        claim(integer->integer == 0xFF);

        integer = lex("1283");
        claim(integer != NULL);
        claim(integer->type == TOKEN_INTEGER);
        claim(integer->integer == 1283);

        integer = lex("1283ssd");

        claim(integer == NULL);
        claim(strstr(lex_error(), "trailing") != NULL);
}

TEST(real)
{
#define almostequal(a, b) (fabs((a) - (b)) <= 0.001)
        struct token *real = lex("10.0");
        claim(real != NULL);
        claim(real->type == TOKEN_REAL);
        claim(almostequal(real->real, 10.0));

        real = lex("0.123");
        claim(real != NULL);
        claim(real->type == TOKEN_REAL);
        claim(almostequal(real->real, 0.123));

        real = lex("0.4");
        claim(real != NULL);
        claim(real->type == TOKEN_REAL);
        claim(almostequal(real->real, 0.4));
#undef almostequal
}

TEST(keyword)
{
        struct token *kw;

        kw = lex("return");
        claim(kw != NULL);
        claim(kw->type == TOKEN_KEYWORD);
        claim(kw->keyword == KEYWORD_RETURN);

        kw = lex("break");
        claim(kw != NULL);
        claim(kw->type == TOKEN_KEYWORD);
        claim(kw->keyword == KEYWORD_BREAK);

        kw = lex("break_ing_news");
        claim(kw != NULL);
        claim(kw->type == TOKEN_IDENTIFIER);
        claim(strcmp(kw->identifier, "break_ing_news") == 0);
}

TEST(invalid_op)
{
        claim(lex("a <$> b;") == NULL);
        claim(strstr(lex_error(), "invalid operator") != NULL);
}

