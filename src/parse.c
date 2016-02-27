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

typedef struct expression *parse_fn();

enum {
        MAX_ERR_LEN  = 2048
};

struct binop {
        struct value (*function)(struct environment *, struct expression const *, struct expression const *);
        char const *op;
        int prec;
};

struct unop {
        struct value (*function)(struct environment *, struct expression const *);
        char const *op;
};

static struct binop binary_operators[] = {
        { .prec = 3, .function = binary_operator_addition,              .op = "+"  },
        { .prec = 3, .function = binary_operator_subtraction,           .op = "-"  },
        { .prec = 4, .function = binary_operator_multiplication,        .op = "*"  },
        { .prec = 3, .function = binary_operator_remainder,             .op = "%"  },
        { .prec = 1, .function = binary_operator_and,                   .op = "&&" },
        { .prec = 1, .function = binary_operator_or,                    .op = "||" },
        { .prec = 2, .function = binary_operator_equality,              .op = "==" },
        { .prec = 2, .function = binary_operator_non_equality,          .op = "!=" },
        { .prec = 2, .function = binary_operator_less_than,             .op = "<"  },
        { .prec = 2, .function = binary_operator_greater_than,          .op = ">"  },
        { .prec = 2, .function = binary_operator_less_than_or_equal,    .op = "<=" },
        { .prec = 2, .function = binary_operator_greater_than_or_equal, .op = ">=" },
};

static struct unop unary_operators[] = {
        { .function = unary_operator_negation, .op = "!" },
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
parse_expr(void);

static struct expression *
parse_e(int);

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

static struct unop
get_unary_operator(char const *op)
{
        size_t n_unops = sizeof unary_operators / sizeof unary_operators[0];

        for (size_t i = 0; i < n_unops; ++i) {
                struct unop o = unary_operators[i];
                if (strcmp(op, o.op) == 0) {
                        return o;
                }
        }

        error("invalid unary operator: %s", op);
}

static struct binop
get_binary_operator(char const *op)
{
        size_t n_binops = sizeof binary_operators / sizeof binary_operators[0];

        for (size_t i = 0; i < n_binops; ++i) {
                struct binop b = binary_operators[i];
                if (strcmp(op, b.op) == 0) {
                        return b;
                }
        }

        error("invalid binary operator: %s", op);
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
        e->identifier = sclone(token->identifier);

        consume(TOKEN_IDENTIFIER);

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
prefix_parenthesized_expression(void)
{
        consume('(');
        struct expression *e = parse_expr();
        consume(')');

        return e;
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
prefix_operator(void)
{
        expect(TOKEN_OPERATOR);
        struct unop op = get_unary_operator(token->operator);
        consume(TOKEN_OPERATOR);

        struct expression *operand = parse_expr();
        struct expression *e = alloc(sizeof *e);

        e->type = EXPRESSION_UNOP;
        e->operand = operand;
        e->unop = op.function;

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
                vec_push(e->elements, parse_expr());
        }

        while (token->type == ',') {
                consume(',');
                vec_push(e->elements, parse_expr());
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
                vec_push(e->keys, parse_expr());
                consume(':');
                vec_push(e->values, parse_expr());
        }

        while (token->type == ',') {
                consume(',');
                vec_push(e->keys, parse_expr());
                consume(':');
                vec_push(e->values, parse_expr());
        }

        consume('}');

        return e;
}
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
                vec_push(e->args, parse_expr());
        }

        while (token->type == ',') {
                consume(',');
                vec_push(e->args, parse_expr());
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
        e->subscript = parse_expr();

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
                vec_push(e->method_args, parse_expr());
        }

        while (token->type == ',') {
                consume(',');
                vec_push(e->method_args, parse_expr());
        }

        consume(')');

        return e;
}

static struct expression *
infix_operator(struct expression *left)
{
        expect(TOKEN_OPERATOR);
        struct binop op = get_binary_operator(token->operator);
        consume(TOKEN_OPERATOR);

        struct expression *right = parse_e(op.prec);

        struct expression *e = alloc(sizeof *e);
        e->type = EXPRESSION_BINOP;
        e->left = left;
        e->right = right;
        e->binop = op.function;

        return e;
}
/* * * * | end of infix parsers | * * * */

static parse_fn *
get_prefix_parser(void)
{
        switch (token->type) {
        case TOKEN_OPERATOR:   return prefix_operator;
        case TOKEN_INTEGER:    return prefix_integer;
        case TOKEN_REAL:       return prefix_real;
        case TOKEN_STRING:     return prefix_string;
        case TOKEN_IDENTIFIER: return prefix_variable;
        case TOKEN_KEYWORD:    goto keyword;
        case '(':              return prefix_parenthesized_expression;
        case '[':              return prefix_array;
        case '{':              return prefix_object;
        default:               goto error;
        }

keyword:

        switch (token->keyword) {
        case KEYWORD_FUNCTION: return prefix_function;
        case KEYWORD_TRUE:     return prefix_true;
        case KEYWORD_FALSE:    return prefix_false;
        case KEYWORD_NIL:      return prefix_nil;
        default:               goto error;
        }

error:
        error("expected expression but found %s", token_show(token));
}

static parse_fn *
get_infix_parser(void)
{
        switch (token->type) {
        case TOKEN_OPERATOR:   return infix_operator;
        case '(':              return infix_function_call;
        case '.':              return infix_member_access;
        case '[':              return infix_subscript;
        default:               error(""); // TODO
        }
}

static int
get_infix_operator_prec(char const *op)
{
        if (strcmp(op, "=") == 0) {
                return -1;
        }

        return get_binary_operator(op).prec;
}

static int
get_infix_prec(void)
{
        switch (token->type) {
        case TOKEN_OPERATOR: return get_infix_operator_prec(token->operator);
        case '[':            return 5;
        case '(':            return 5;
        case '.':            return 6;
        default:             return -1;
        }
}

static struct lvalue *
definition_lvalue(struct expression const *e)
{
        struct lvalue *lv = alloc(sizeof *lv);

        if (e->type == EXPRESSION_VARIABLE) {
                lv->type = LVALUE_NAME;
                lv->name = sclone(e->identifier);
        } else if (e->type == EXPRESSION_ARRAY) {
                lv->type = LVALUE_ARRAY;
                vec_init(lv->lvalues);
                for (size_t i = 0; i < e->elements.count; ++i) {
                        vec_push(lv->lvalues, definition_lvalue(e->elements.items[i]));
                }
        } else {
                error("invalid lvalue in let assignment statement");
        }

        return lv;
}

static struct lvalue *
assignment_lvalue(struct expression const *e)
{
        struct lvalue *lv = alloc(sizeof *lv);

        if (e->type == EXPRESSION_VARIABLE) {
                lv->type = LVALUE_NAME;
                lv->name = sclone(e->identifier);
        } else if (e->type == EXPRESSION_ARRAY) {
                lv->type = LVALUE_ARRAY;
                vec_init(lv->lvalues);
                for (size_t i = 0; i < e->elements.count; ++i) {
                        vec_push(lv->lvalues, assignment_lvalue(e->elements.items[i]));
                }
        } else if (e->type == EXPRESSION_SUBSCRIPT) {
                lv->type = LVALUE_SUBSCRIPT;
                lv->container = e->container;
                lv->subscript = e->subscript;
        } else if (e->type == EXPRESSION_MEMBER_ACCESS) {
                lv->type = LVALUE_MEMBER;
                lv->object = e->object;
                lv->member_name = sclone(e->member_name);
        } else {
                error("invalid lvalue in assignment expression");
        }

        return lv;
}

static struct statement *
parse_for_loop()
{
        consume_keyword(KEYWORD_FOR);

        struct statement *s = alloc(sizeof *s);
        s->type = STATEMENT_FOR_LOOP;

        consume('(');

        s->for_loop.init = parse_statement();

        if (token->type == ';') {
                s->for_loop.cond = NULL;
        } else {
                s->for_loop.cond = parse_expr();
        }

        consume(';');

        if (token->type == ')') {
                s->for_loop.next = NULL;
        } else {
                s->for_loop.next = parse_expr();
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
        s->while_loop.cond = parse_expr();
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
        s->conditional.cond = parse_expr();
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
        s->return_value = parse_expr();

        consume(';');

        return s;

}

static struct statement *
parse_let_definition()
{
        consume_keyword(KEYWORD_LET);

        struct lvalue *target = definition_lvalue(parse_e(0));

        if (token->type != TOKEN_OPERATOR || strcmp(token->operator, "=") != 0) {
                error("expected %s after lvalue in assignment statement but found %s", token_show_type('='), token_show(token));
        }

        consume(TOKEN_OPERATOR);

        struct expression *value = parse_expr();

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
parse_e(int prec)
{
        struct expression *e;

        parse_fn *f = get_prefix_parser();
        if (f == NULL) {
                error("expected expression but found %s", ""); // TODO
        }

        e = f();

        while (prec < get_infix_prec()) {
                f = get_infix_parser();
                if (f == NULL) {
                        error("expected operator but found %s", ""); // TODO
                }
                e = f(e);
        }

        return e;
}

inline static struct expression *
parse_expr(void)
{
        struct expression *e = parse_e(0);

        if (token->type == TOKEN_OPERATOR && strcmp(token->operator, "=") == 0) {
                consume(TOKEN_OPERATOR);
                struct expression *assignment = alloc(sizeof *assignment);
                assignment->type = EXPRESSION_ASSIGNMENT;
                assignment->target = assignment_lvalue(e);
                assignment->value = parse_expr();
                return assignment;
        } else {
                return e;
        }
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
                s->expression = parse_expr();
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
        default:               error("expected statement but found %s", ""); // TODO
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

        return parse_expr();
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

TEST(invalid_op)
{
        claim(parse(lex("a <$> b;")) == NULL);
        claim(strstr(parse_error(), "binary operator") != NULL);
}

TEST(plus_op)
{
        struct statement *s = parse(lex("a + 4 + f();"))[0];
        claim(s->type == STATEMENT_EXPRESSION);
        claim(s->expression->type == EXPRESSION_BINOP);
        claim(s->expression->binop == binary_operator_addition);
        claim(s->expression->left->type == EXPRESSION_BINOP);
        claim(s->expression->left->left->type == EXPRESSION_VARIABLE);
        claim(s->expression->left->right->type == EXPRESSION_INTEGER);
}

TEST(object_literal)
{
        
        struct statement *s = parse(lex("1 + {};"))[0];
        claim(s->type == STATEMENT_EXPRESSION);
        claim(s->expression->type == EXPRESSION_BINOP);
        claim(s->expression->binop == binary_operator_addition);
        claim(s->expression->right->type == EXPRESSION_OBJECT);
        claim(s->expression->right->keys.count == 0);
        claim(s->expression->right->values.count == 0);

        s = parse(lex("1 + {4 + 3: 'test'};"))[0];
        claim(s->type == STATEMENT_EXPRESSION);
        claim(s->expression->type == EXPRESSION_BINOP);
        claim(s->expression->binop == binary_operator_addition);
        claim(s->expression->right->type == EXPRESSION_OBJECT);
        claim(s->expression->right->keys.count == 1);
        claim(s->expression->right->values.count == 1);
        claim(s->expression->right->keys.items[0]->type == EXPRESSION_BINOP);
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

        claim(parse(lex("let a[3] = 5")) == NULL);
        claim(strstr(parse_error(), "invalid lvalue in let assignment statement") != NULL);
}
