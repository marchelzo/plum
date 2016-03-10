#include <setjmp.h>
#include <stdarg.h>
#include <string.h>
#include <stdbool.h>
#include <assert.h>
#include <errno.h>
#include <math.h>
#include <stdnoreturn.h>

#include "vec.h"
#include "token.h"
#include "test.h"
#include "ast.h"
#include "util.h"
#include "alloc.h"
#include "lex.h"
#include "operators.h"
#include "value.h"
#include "log.h"
#include "vm.h"

#define BINARY_OPERATOR(name, token, prec, right_assoc) \
        static struct expression * \
        infix_ ## name(struct expression *left) \
        { \
                consume(TOKEN_ ## token); \
                struct expression *e = alloc(sizeof *e); \
                e->type = EXPRESSION_ ## token; \
                e->left = left; \
                e->right = parse_expr(prec - (right_assoc ? 1 : 0)); \
                return e; \
        } \

#define PREFIX_OPERATOR(name, token, prec) \
        static struct expression * \
        prefix_ ## name(void) \
        { \
                consume(TOKEN_ ## token); \
                struct expression *e = alloc(sizeof *e); \
                e->type = EXPRESSION_PREFIX_ ## token; \
                e->operand = parse_expr(prec); \
                return e; \
        } \

typedef struct expression *parse_fn();

enum {
        MAX_ERR_LEN  = 2048
};

static jmp_buf jb;
static char errbuf[MAX_ERR_LEN + 1];
static struct token *token;

static struct statement BREAK_STATEMENT    = { .type = STATEMENT_BREAK    };
static struct statement CONTINUE_STATEMENT = { .type = STATEMENT_CONTINUE };
static struct statement NULL_STATEMENT     = { .type = STATEMENT_NULL     };

static struct statement *
parse_statement(void);

static struct expression *
parse_expr(int);

static struct expression *
assignment_lvalue(struct expression *e);

noreturn static void
error(char const *fmt, ...)
{
        va_list ap;
        va_start(ap, fmt);

        int n = 0;

        char *err = errbuf;
        n += sprintf(err, "Parse error at %zu:%zu: ", token->loc.line + 1, token->loc.col + 1);
        vsnprintf(err + n, MAX_ERR_LEN - n, fmt, ap);

        va_end(ap);

        longjmp(jb, 1);
}

static void
expect(int type)
{
        if (token->type != type) {
                error("expected %s but found %s", token_show_type(type), token_show(token));
        }
}


static void
expect_keyword(int type)
{
        if (token->type != TOKEN_KEYWORD || token->keyword != type) {
                error("expected %s but found %s", token_show(&(struct token){ .type = TOKEN_KEYWORD, .keyword = type }), token_show(token));
        }
}

inline static void
consume(int type)
{
        expect(type);
        token += 1;

}

inline static void
consume_keyword(int type)
{
        expect_keyword(type);
        token += 1;

}

/* * * * | prefix parsers | * * * */
static struct expression *
prefix_integer(void)
{
        expect(TOKEN_INTEGER);

        struct expression *e = alloc(sizeof *e);
        e->type = EXPRESSION_INTEGER;
        e->integer = token->integer;

        consume(TOKEN_INTEGER);

        return e;
}

static struct expression *
prefix_real(void)
{
        expect(TOKEN_REAL);

        struct expression *e = alloc(sizeof *e);
        e->type = EXPRESSION_REAL;
        e->real = token->real;

        consume(TOKEN_REAL);

        return e;
}

static struct expression *
prefix_string(void)
{
        expect(TOKEN_STRING);

        struct expression *e = alloc(sizeof *e);
        e->type = EXPRESSION_STRING;
        e->string = token->string;

        consume(TOKEN_STRING);

        return e;
}

static struct expression *
prefix_variable(void)
{
        expect(TOKEN_IDENTIFIER);

        struct expression *e = alloc(sizeof *e);
        e->type = EXPRESSION_VARIABLE;
        e->identifier = token->identifier;
        consume(TOKEN_IDENTIFIER);

        if (token->type == ':' && token[1].type == ':') {
                vec(char) module;
                vec_init(module);
                e->type = EXPRESSION_MODULE_ACCESS;
                while (token->type == ':' && token[1].type == ':') {
                        if (module.count > 0) {
                                vec_push(module, '/');
                        }
                        vec_push_n(module, e->identifier, strlen(e->identifier));
                        consume(':');
                        consume(':');
                        expect(TOKEN_IDENTIFIER);
                        e->identifier = token->identifier;
                        consume(TOKEN_IDENTIFIER);
                }
                vec_push(module, '\0');
                e->module = module.items;
        }

        return e;
}

static struct expression *
prefix_function(void)
{
        consume_keyword(KEYWORD_FUNCTION);

        struct expression *e = alloc(sizeof *e);
        e->type = EXPRESSION_FUNCTION;
        vec_init(e->params);

        consume('(');

        if (token->type == ')') {
                consume(')');
                goto body;
        } else {
                expect(TOKEN_IDENTIFIER);
                vec_push(e->params, sclone(token->identifier));
                consume(TOKEN_IDENTIFIER);
        }

        while (token->type == ',') {
                consume(',');
                expect(TOKEN_IDENTIFIER);
                vec_push(e->params, sclone(token->identifier));
                consume(TOKEN_IDENTIFIER);
        }

        consume(')');

body:

        e->body = parse_statement();

        return e;
}

static struct expression *
prefix_parenthesis(void)
{
        /*
         * This can either be a plain old parenthesized expression, e.g., (4 + 4)
         * or it can be an identifier list for an arrow function, e.g., (a, b, c).
         */

        consume('(');

        /*
         * () is an empty identifier list.
         */
        if (token->type == ')') {
                consume(')');
                struct expression *list = alloc(sizeof *list);
                list->type = EXPRESSION_IDENTIFIER_LIST;
                vec_init(list->identifiers);
                return list;
        }

        struct expression *e = parse_expr(0);

        if (token->type == ',') {
                /*
                 * It _must_ be an identifier list.
                 */
                if (e->type != EXPRESSION_VARIABLE) {
                        error("non-identifier in identifier list");
                }

                struct expression *list = alloc(sizeof *list);
                list->type = EXPRESSION_IDENTIFIER_LIST;
                vec_init(list->identifiers);
                vec_push(list->identifiers, e->identifier);
                free(e);

                while (token->type == ',') {
                        consume(',');
                        expect(TOKEN_IDENTIFIER);
                        vec_push(list->identifiers, token->identifier);
                        consume(TOKEN_IDENTIFIER);
                }

                consume(')');

                return list;
        } else {
                consume(')');
                return e;
        }
}

static struct expression *
prefix_true(void)
{
        consume_keyword(KEYWORD_TRUE);

        struct expression *e = alloc(sizeof *e);
        e->type = EXPRESSION_BOOLEAN;
        e->boolean = true;

        return e;
}

static struct expression *
prefix_false(void)
{
        consume_keyword(KEYWORD_FALSE);

        struct expression *e = alloc(sizeof *e);
        e->type = EXPRESSION_BOOLEAN;
        e->boolean = false;

        return e;
}

static struct expression *
prefix_nil(void)
{
        consume_keyword(KEYWORD_NIL);

        struct expression *e = alloc(sizeof *e);
        e->type = EXPRESSION_NIL;

        return e;
}

static struct expression *
prefix_array(void)
{
        consume('[');

        struct expression *e = alloc(sizeof *e);
        e->type = EXPRESSION_ARRAY;
        vec_init(e->elements);

        if (token->type == ']') {
                consume(']');
                return e;
        } else {
                vec_push(e->elements, parse_expr(0));
        }

        while (token->type == ',') {
                consume(',');
                vec_push(e->elements, parse_expr(0));
        }

        consume(']');

        return e;
}

static struct expression *
prefix_object(void)
{
        consume('{');

        struct expression *e = alloc(sizeof *e);
        e->type = EXPRESSION_OBJECT;

        vec_init(e->keys);
        vec_init(e->values);

        if (token->type == '}') {
                consume('}');
                return e;
        } else {
                vec_push(e->keys, parse_expr(0));
                consume(':');
                vec_push(e->values, parse_expr(0));
        }

        while (token->type == ',') {
                consume(',');
                vec_push(e->keys, parse_expr(0));
                consume(':');
                vec_push(e->values, parse_expr(0));
        }

        consume('}');

        return e;
}

PREFIX_OPERATOR(at,    AT,    4)
PREFIX_OPERATOR(minus, MINUS, 4)
PREFIX_OPERATOR(bang,  BANG,  4)
PREFIX_OPERATOR(inc,   INC,   6)
PREFIX_OPERATOR(dec,   DEC,   6)
/* * * * | end of prefix parsers | * * * */

/* * * * | infix parsers | * * * */
static struct expression *
infix_function_call(struct expression *left)
{
        struct expression *e = alloc(sizeof *e);
        e->type = EXPRESSION_FUNCTION_CALL;
        e->function = left;
        vec_init(e->args);

        consume('(');

        if (token->type == ')') {
                consume(')');
                return e;
        } else {
                vec_push(e->args, parse_expr(0));
        }

        while (token->type == ',') {
                consume(',');
                vec_push(e->args, parse_expr(0));
        }

        consume(')');

        return e;
}

static struct expression *
infix_subscript(struct expression *left)
{
        consume('[');
        
        struct expression *e = alloc(sizeof *e);
        e->type = EXPRESSION_SUBSCRIPT;
        e->container = left;
        e->subscript = parse_expr(0);

        consume(']');

        return e;
}

static struct expression *
infix_member_access(struct expression *left)
{
        consume('.');
        
        struct expression *e = alloc(sizeof *e);
        e->object = left;

        expect(TOKEN_IDENTIFIER);

        if (token[1].type != '(') {
                e->type = EXPRESSION_MEMBER_ACCESS;
                e->member_name = sclone(token->identifier);
                consume(TOKEN_IDENTIFIER);
                return e;
        }

        e->type = EXPRESSION_METHOD_CALL;
        e->method_name = sclone(token->identifier);
        consume(TOKEN_IDENTIFIER);
        vec_init(e->method_args);

        consume('(');

        if (token->type == ')') {
                consume(')');
                return e;
        } else {
                vec_push(e->method_args, parse_expr(0));
        }

        while (token->type == ',') {
                consume(',');
                vec_push(e->method_args, parse_expr(0));
        }

        consume(')');

        return e;
}

static struct expression *
infix_arrow_function(struct expression *left)
{
        if (left->type != EXPRESSION_VARIABLE && left->type != EXPRESSION_IDENTIFIER_LIST) {
                error("non-identifier used in identifier list");
        }

        consume(TOKEN_ARROW);

        struct expression *e = alloc(sizeof *e);
        e->type = EXPRESSION_FUNCTION;
        vec_init(e->params);

        if (left->type == EXPRESSION_VARIABLE) {
                vec_push(e->params, left->identifier);
        } else {
                e->params.items = left->identifiers.items;
                e->params.count = left->identifiers.count;
        }

        free(left);

        struct statement *ret = alloc(sizeof *ret);
        ret->type = STATEMENT_RETURN;
        ret->return_value = parse_expr(0);

        e->body = ret;

        return e;

}

static struct expression *
infix_assignment(struct expression *left)
{
        struct expression *e = alloc(sizeof *e);

        consume(TOKEN_EQ);

        e->type = EXPRESSION_ASSIGNMENT;
        e->target = assignment_lvalue(left);
        e->value = parse_expr(0);
        
        return e;
}

static struct expression *
postfix_inc(struct expression *left)
{
        struct expression *e = alloc(sizeof *e);

        consume(TOKEN_INC);

        e->type = EXPRESSION_POSTFIX_INC;
        e->operand = assignment_lvalue(left);
        
        return e;
}

static struct expression *
postfix_dec(struct expression *left)
{
        struct expression *e = alloc(sizeof *e);

        consume(TOKEN_DEC);

        e->type = EXPRESSION_POSTFIX_DEC;
        e->operand = assignment_lvalue(left);
        
        return e;
}

BINARY_OPERATOR(star,    STAR,    6, false)
BINARY_OPERATOR(div,     DIV,     6, true )
BINARY_OPERATOR(percent, PERCENT, 6, false)

BINARY_OPERATOR(plus,    PLUS,    5, false)
BINARY_OPERATOR(minus,   MINUS,   5, false)

BINARY_OPERATOR(lt,      LT,      4, false)
BINARY_OPERATOR(gt,      GT,      4, false)
BINARY_OPERATOR(geq,     GEQ,     4, false)
BINARY_OPERATOR(leq,     LEQ,     4, false)

BINARY_OPERATOR(not_eq,  NOT_EQ,  3, false)
BINARY_OPERATOR(dbl_eq,  DBL_EQ,  3, false)

BINARY_OPERATOR(or,      OR,      2, false)
BINARY_OPERATOR(and,    AND,      2, false)
/* * * * | end of infix parsers | * * * */

static parse_fn *
get_prefix_parser(void)
{
        switch (token->type) {
        case TOKEN_INTEGER:    return prefix_integer;
        case TOKEN_REAL:       return prefix_real;
        case TOKEN_STRING:     return prefix_string;

        case TOKEN_IDENTIFIER: return prefix_variable;
        case TOKEN_KEYWORD:    goto keyword;

        case '(':              return prefix_parenthesis;
        case '[':              return prefix_array;
        case '{':              return prefix_object;

        case TOKEN_BANG:       return prefix_bang;
        case TOKEN_AT:         return prefix_at;
        case TOKEN_MINUS:      return prefix_minus;
        case TOKEN_INC:        return prefix_inc;
        case TOKEN_DEC:        return prefix_dec;

        default:               return NULL;
        }

keyword:

        switch (token->keyword) {
        case KEYWORD_FUNCTION: return prefix_function;
        case KEYWORD_TRUE:     return prefix_true;
        case KEYWORD_FALSE:    return prefix_false;
        case KEYWORD_NIL:      return prefix_nil;
        default:               return NULL;
        }
}

static parse_fn *
get_infix_parser(void)
{
        switch (token->type) {
        case '(':             return infix_function_call;
        case '.':             return infix_member_access;
        case '[':             return infix_subscript;
        case TOKEN_INC:       return postfix_inc;
        case TOKEN_DEC:       return postfix_dec;
        case TOKEN_ARROW:     return infix_arrow_function;
        case TOKEN_EQ:        return infix_assignment;
        case TOKEN_STAR:      return infix_star;
        case TOKEN_DIV:       return infix_div;
        case TOKEN_PERCENT:   return infix_percent;
        case TOKEN_PLUS:      return infix_plus;
        case TOKEN_MINUS:     return infix_minus;
        case TOKEN_LT:        return infix_lt;
        case TOKEN_GT:        return infix_gt;
        case TOKEN_GEQ:       return infix_geq;
        case TOKEN_LEQ:       return infix_leq;
        case TOKEN_NOT_EQ:    return infix_not_eq;
        case TOKEN_DBL_EQ:    return infix_dbl_eq;
        case TOKEN_OR:        return infix_or;
        case TOKEN_AND:       return infix_and;
        default:              return NULL;
        }
}

static int
get_infix_prec(void)
{
        switch (token->type) {
        case '.':           return 9;

        case '[':           return 8;
        case '(':           return 8;

        case TOKEN_INC:     return 7;
        case TOKEN_DEC:     return 7;

        case TOKEN_PERCENT: return 6;
        case TOKEN_DIV:     return 6;
        case TOKEN_STAR:    return 6;

        case TOKEN_MINUS:   return 5;
        case TOKEN_PLUS:    return 5;

        case TOKEN_GEQ:     return 4;
        case TOKEN_LEQ:     return 4;
        case TOKEN_GT:      return 4;
        case TOKEN_LT:      return 4;

        case TOKEN_NOT_EQ:  return 3;
        case TOKEN_DBL_EQ:  return 3;

        case TOKEN_OR:      return 2;
        case TOKEN_AND:     return 2;

        case TOKEN_EQ:      return 1;
        case TOKEN_ARROW:   return 1;

        default:            return -1;
        }
}

static struct expression *
assignment_lvalue(struct expression *e)
{
        switch (e->type) {
        case EXPRESSION_VARIABLE:
        case EXPRESSION_SUBSCRIPT:
        case EXPRESSION_MEMBER_ACCESS:
                return e;
        case EXPRESSION_ARRAY:
                for (size_t i = 0; i < e->elements.count; ++i) {
                        e->elements.items[i] = assignment_lvalue(e->elements.items[i]);
                }
                return e;
        default:
                error("expression is not an lvalue");
        }
}

/*
 * This is kind of a hack.
 */
static struct expression *
parse_definition_lvalue(int expect, int kw)
{
        struct expression *e, *elem;
        struct token *save = token;

        switch (token->type) {
        case TOKEN_IDENTIFIER:
                e = alloc(sizeof *e);
                e->type = EXPRESSION_VARIABLE;
                e->identifier = token->identifier;
                consume(TOKEN_IDENTIFIER);
                break;
        case '[':
                consume('[');
                e = alloc(sizeof *e);
                e->type = EXPRESSION_ARRAY;
                vec_init(e->elements);
                if (token->type == ']') {
                        consume(']');
                        break;
                } else {
                        if (elem = parse_definition_lvalue(-1, -1), elem == NULL) {
                                vec_empty(e->elements);
                                goto error;
                        } else {
                                vec_push(e->elements, elem);
                        }
                }
                while (token->type == ',') {
                        consume(',');
                        if (elem = parse_definition_lvalue(-1, -1), elem == NULL) {
                                vec_empty(e->elements);
                                goto error;
                        } else {
                                vec_push(e->elements, elem);
                        }

                }
                if (token->type != ']') {
                        vec_empty(e->elements);
                        goto error;
                }

                consume(']');
                break;
        case '(':
                consume('(');
                e = parse_definition_lvalue(-1, -1);
                if (e == NULL || token->type != ')') {
                        goto error;
                }
                consume(')');
                break;
        default:
                return NULL;
        }

        if (((expect != -1) && (token->type != expect)) || (expect == TOKEN_KEYWORD && token->keyword != kw)) {
                goto error;
        }

        return e;

error:
        free(e);
        token = save;
        return NULL;
}

static struct statement *
parse_for_loop(void)
{
        consume_keyword(KEYWORD_FOR);

        struct statement *s = alloc(sizeof *s);
        s->type = STATEMENT_FOR_LOOP;

        consume('(');

        /*
         * First try to parse this as a for-each loop. If that fails, assume it's
         * a C-style for loop.
         */
        struct expression *each_target = parse_definition_lvalue(TOKEN_KEYWORD, KEYWORD_IN);
        if (each_target != NULL) {
                s->type = STATEMENT_EACH_LOOP;
                s->each.target = each_target;

                consume_keyword(KEYWORD_IN);

                s->each.array = parse_expr(0);

                consume(')');

                s->each.body = parse_statement();

                return s;
        }

        s->for_loop.init = parse_statement();

        if (token->type == ';') {
                s->for_loop.cond = NULL;
        } else {
                s->for_loop.cond = parse_expr(0);
        }

        consume(';');

        if (token->type == ')') {
                s->for_loop.next = NULL;
        } else {
                s->for_loop.next = parse_expr(0);
        }

        consume(')');

        s->for_loop.body = parse_statement();

        return s;
}

static struct statement *
parse_while_loop()
{
        consume_keyword(KEYWORD_WHILE);

        struct statement *s = alloc(sizeof *s);
        s->type = STATEMENT_WHILE_LOOP;

        consume('(');
        s->while_loop.cond = parse_expr(0);
        consume(')');

        s->while_loop.body = parse_statement();

        return s;
}

static struct statement *
parse_if_statement()
{
        consume_keyword(KEYWORD_IF);

        struct statement *s = alloc(sizeof *s);
        s->type = STATEMENT_CONDITIONAL;

        consume('(');
        s->conditional.cond = parse_expr(0);
        consume(')');

        s->conditional.then_branch = parse_statement();

        if (token->type != TOKEN_KEYWORD || token->keyword != KEYWORD_ELSE) {
                s->conditional.else_branch = NULL;
                return s;
        }

        consume_keyword(KEYWORD_ELSE);

        s->conditional.else_branch = parse_statement();

        return s;
}

static struct statement *
parse_function_def()
{
        consume_keyword(KEYWORD_FUNCTION);

        struct statement *s = alloc(sizeof *s);
        s->type = STATEMENT_FUNCTION_DEFINITION;
        vec_init(s->function.params);

        expect(TOKEN_IDENTIFIER);
        s->function.name = sclone(token->identifier);
        consume(TOKEN_IDENTIFIER);

        consume('(');

        if (token->type == ')') {
                consume(')');
                goto body;
        } else {
                expect(TOKEN_IDENTIFIER);
                vec_push(s->function.params, sclone(token->identifier));
                consume(TOKEN_IDENTIFIER);
        }

        while (token->type == ',') {
                consume(',');
                expect(TOKEN_IDENTIFIER);
                vec_push(s->function.params, sclone(token->identifier));
                consume(TOKEN_IDENTIFIER);
        }

        consume(')');

body:

        s->function.body = parse_statement();
        
        return s;
}

static struct statement *
parse_return_statement()
{
        consume_keyword(KEYWORD_RETURN);

        struct statement *s = alloc(sizeof *s);
        s->type = STATEMENT_RETURN;
        s->return_value = parse_expr(0);

        consume(';');

        return s;

}

static struct statement *
parse_let_definition()
{
        consume_keyword(KEYWORD_LET);

        struct expression *target = parse_definition_lvalue(TOKEN_EQ, -1);
        if (target == NULL) {
                error("failed to parse lvalue in 'let' definition");
        }

        consume(TOKEN_EQ);

        struct expression *value = parse_expr(0);
        
        consume(';');

        struct statement *s = alloc(sizeof *s);
        s->type = STATEMENT_DEFINITION;
        s->target = target;
        s->value = value;

        return s;
}

static struct statement *
parse_break_statement()
{
        consume_keyword(KEYWORD_BREAK);
        consume(';');
        return &BREAK_STATEMENT;
}

static struct statement *
parse_continue_statement()
{
        consume_keyword(KEYWORD_CONTINUE);
        consume(';');
        return &CONTINUE_STATEMENT;
}

static struct statement *
parse_null_statement()
{
        consume(';');
        return &NULL_STATEMENT;
}

static struct expression *
parse_expr(int prec)
{
        struct expression *e;

        parse_fn *f = get_prefix_parser();
        if (f == NULL) {
                error("expected expression but found %s", token_show(token));
        }

        e = f();

        while (prec < get_infix_prec()) {
                f = get_infix_parser();
                if (f == NULL) {
                        error("unexpected token after expression: %s", token_show(token));
                }
                e = f(e);
        }

        return e;
}

static struct statement *
parse_block(void)
{
        struct statement *s = alloc(sizeof *s);

        consume('{');

        s->type = STATEMENT_BLOCK;
        vec_init(s->statements);

        while (token->type != '}') {
                vec_push(s->statements, parse_statement());
        }

        consume('}');

        return s;
}

static struct statement *
parse_import(void)
{
        struct statement *s = alloc(sizeof *s);
        s->type = STATEMENT_IMPORT;

        consume_keyword(KEYWORD_IMPORT);

        vec(char) module;
        vec_init(module);

        expect(TOKEN_IDENTIFIER);
        vec_push_n(module, token->identifier, strlen(token->identifier));
        consume(TOKEN_IDENTIFIER);

        while (token->type == ':') {
                vec_push(module, '/');
                consume(':');
                consume(':');
                expect(TOKEN_IDENTIFIER);
                vec_push_n(module, token->identifier, strlen(token->identifier));
                consume(TOKEN_IDENTIFIER);
        }

        vec_push(module, '\0');
        s->import.module = module.items;

        // TODO: maybe make 'as' an actual keyword
        if (token->type == TOKEN_IDENTIFIER && strcmp(token->identifier, "as") == 0) {
                consume(TOKEN_IDENTIFIER);
                expect(TOKEN_IDENTIFIER);
                s->import.as = token->identifier;
                consume(TOKEN_IDENTIFIER);
        } else {
                s->import.as = s->import.module;
        }

        consume(';');

        return s;
}

static struct statement *
parse_statement(void)
{
        struct statement *s;

        switch (token->type) {
        case '{':            return parse_block();
        case ';':            return parse_null_statement();
        case TOKEN_KEYWORD:  goto keyword;
        default: // expression
                s = alloc(sizeof *s);
                s->type = STATEMENT_EXPRESSION;
                s->expression = parse_expr(0);
                consume(';');
                return s;
        }

keyword:

        switch (token->keyword) {
        case KEYWORD_FOR:      return parse_for_loop();
        case KEYWORD_WHILE:    return parse_while_loop();
        case KEYWORD_IF:       return parse_if_statement();
        case KEYWORD_FUNCTION: return parse_function_def();
        case KEYWORD_RETURN:   return parse_return_statement();
        case KEYWORD_LET:      return parse_let_definition();
        case KEYWORD_BREAK:    return parse_break_statement();
        case KEYWORD_CONTINUE: return parse_continue_statement();
        default:               error("expected statement but found %s", token_show(token));
        }
}

char const *
parse_error(void)
{
        return errbuf;
}

struct statement **
parse(struct token *tokens)
{
        vec(struct statement *) program;
        vec_init(program);
        token = tokens;

        if (setjmp(jb) != 0) {
                vec_empty(program);
                return NULL;
        }

        while (token->type == TOKEN_KEYWORD && token->keyword == KEYWORD_IMPORT) {
                vec_push(program, parse_import());
        }
        while (token->type != TOKEN_END) {
                vec_push(program, parse_statement());
        }

        vec_push(program, NULL);

        return program.items;
}

struct expression *
parse_expression(struct token *tokens)
{
        token = tokens;

        if (setjmp(jb) != 0) {
                return NULL;
        }

        return parse_expr(0);
}

TEST(break_statement)
{
        char const *source = "break;";
        struct token *ts = lex(source);
        claim(ts != NULL);
        claim(ts->type == TOKEN_KEYWORD);
        claim(ts->keyword == KEYWORD_BREAK);

        struct statement **p = parse(ts);
        struct statement *s = p[0];

        claim(p[1] == NULL);

        claim(s != NULL);
        claim(s->type == STATEMENT_BREAK);
        claim(s == &BREAK_STATEMENT);
}

TEST(parse_error)
{
        char const *source = "function a (,) { }";
        struct token *ts = lex(source);
        claim(ts != NULL);

        struct statement **p = parse(ts);
        claim(p == NULL);

        claim(strstr(parse_error(), "but found token ','"));
}

TEST(trivial_function)
{
        claim(parse(lex("function f();")) != NULL);
}

TEST(for_loop)
{
        claim(parse(lex("for (;;) {}")) != NULL);
}

TEST(number)
{
        claim(parse(lex("45.3;")) != NULL);
}

TEST(string)
{
        claim(parse(lex("'hello, world!';")) != NULL);
}

TEST(function_call)
{
        struct statement **s;

        claim((s = parse(lex("f();"))) != NULL);

        claim((s = parse(lex("f(43);"))) != NULL);

        claim((s = parse(lex("f(a, b, g(c));"))) != NULL);
}

TEST(parenthesized_expression)
{
        claim(parse(lex("(((3)));")) != NULL);
}

TEST(plus_op)
{
        struct statement *s = parse(lex("a + 4 + f();"))[0];
        claim(s->type == STATEMENT_EXPRESSION);
        claim(s->expression->type == EXPRESSION_PLUS);
        claim(s->expression->left->type == EXPRESSION_PLUS);
        claim(s->expression->left->left->type == EXPRESSION_VARIABLE);
        claim(s->expression->left->right->type == EXPRESSION_INTEGER);
}

TEST(object_literal)
{
        
        struct statement *s = parse(lex("1 + {};"))[0];
        claim(s->type == STATEMENT_EXPRESSION);
        claim(s->expression->type == EXPRESSION_PLUS);
        claim(s->expression->right->type == EXPRESSION_OBJECT);
        claim(s->expression->right->keys.count == 0);
        claim(s->expression->right->values.count == 0);

        s = parse(lex("1 + {4 + 3: 'test'};"))[0];
        claim(s->type == STATEMENT_EXPRESSION);
        claim(s->expression->type == EXPRESSION_PLUS);
        claim(s->expression->right->type == EXPRESSION_OBJECT);
        claim(s->expression->right->keys.count == 1);
        claim(s->expression->right->values.count == 1);
        claim(s->expression->right->keys.items[0]->type == EXPRESSION_PLUS);
        claim(s->expression->right->values.items[0]->type == EXPRESSION_STRING);
}

TEST(array_literal)
{
        struct statement *s = parse(lex("[1, 2, 3];"))[0];
        claim(s->type == STATEMENT_EXPRESSION);
        claim(s->expression->type == EXPRESSION_ARRAY);
        claim(s->expression->elements.count == 3);
}

TEST(test_programs)
{
        char *test_progs[] = {
                "tests/programs/hello",
                "tests/programs/factorial",
                "tests/programs/closure",
        };

        for (size_t i = 0; i < sizeof test_progs / sizeof test_progs[0]; ++i) {
                struct token *tokens = lex(slurp(test_progs[i]));
                if (tokens == NULL) {
                        printf("\n%s: %s\n", test_progs[i], lex_error());
                        claim(false);
                }
                struct statement **program = parse(tokens);
                if (program == NULL) {
                        printf("\n%s: %s\n", test_progs[i], parse_error());
                        claim(false);
                }
        }
}

TEST(method_call)
{
        struct token *ts = lex("Buffer.verticalSplit();");
        claim(ts != NULL);

        struct statement **p = parse(ts);
        claim(p != NULL);

        struct statement *s = p[0];
        claim(s->type == STATEMENT_EXPRESSION);
        claim(s->expression->type == EXPRESSION_METHOD_CALL);
        claim(s->expression->object->type == EXPRESSION_VARIABLE);
        claim(strcmp(s->expression->object->identifier, "Buffer") == 0);
        claim(strcmp(s->expression->method_name, "verticalSplit") == 0);
        claim(s->expression->method_args.count == 0);
}

TEST(let)
{
        claim(parse(lex("let a = 12;")) != NULL);
        claim(parse(lex("let [a, b] = [12, 14];")) != NULL);
        claim(parse(lex("let [[a1, a2], b] = [[12, 19], 14];")) != NULL);

        claim(parse(lex("let a[3] = 5;")) == NULL);
        claim(strstr(parse_error(), "failed to parse lvalue in 'let' definition") != NULL);
}

TEST(each_loop)
{
        struct token *ts = lex("for (a in as) print(a);");
        claim(ts != NULL);

        struct statement **p = parse(ts);
        claim(p != NULL);

        struct statement *s = p[0];
        claim(s->type == STATEMENT_EACH_LOOP);
        claim(s->each.array->type == EXPRESSION_VARIABLE);
        claim(s->each.target->type == EXPRESSION_VARIABLE);
        claim(s->each.body->type == STATEMENT_EXPRESSION);
        claim(s->each.body->expression->type == EXPRESSION_FUNCTION_CALL);
}

TEST(arrow)
{
        struct token *ts = lex("let f = (a, b) -> a + b;");
        claim(ts != NULL);

        struct statement **p = parse(ts);
        claim(p != NULL);

        struct statement *s = p[0];
        claim(s->type == STATEMENT_DEFINITION);
        claim(s->value->type == EXPRESSION_FUNCTION);
        claim(s->value->params.count == 2);
        claim(s->value->body->type == STATEMENT_RETURN);
        claim(s->value->body->return_value->type == EXPRESSION_PLUS);
}

TEST(import)
{
        struct token *ts = lex("import editor::buffer as buffer;");
        claim(ts != NULL);

        struct statement **p = parse(ts);
        printf("%s\n", parse_error());
        claim(p != NULL);

        struct statement *s = p[0];
        claim(s->type == STATEMENT_IMPORT);
        claim(strcmp(s->import.module, "editor/buffer") == 0);
        claim(strcmp(s->import.as, "buffer") == 0);
}
