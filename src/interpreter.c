#include <setjmp.h>
#include <string.h>
#include <stdbool.h>
#include <stdio.h>

#include "eval.h"
#include "interpreter.h"
#include "value.h"
#include "ast.h"
#include "environment.h"
#include "builtin_functions.h"
#include "util.h"
#include "test.h"
#include "lex.h"
#include "parse.h"

static struct environment *global_env = NULL;

static jmp_buf jb;

static char const *err_msg;

static void
panic_hook(void)
{
        longjmp(jb, 1);
}

void
interpreter_init(void)
{
        eval_set_panic_hook(panic_hook);
        global_env = env_new(NULL);
        env_insert(global_env, "print", (struct value){ .type = VALUE_BUILTIN_FUNCTION, .builtin_function = builtin_print });
}

bool
interpreter_execute_source(char const *source)
{
        struct token *ts = lex(source);
        if (ts == NULL) {
                err_msg = lex_error();
                return false;
        }

        struct statement **p = parse(ts);
        if (p == NULL) {
                err_msg = parse_error();
                return false;
        }

        if (setjmp(jb) != 0) {
                err_msg = eval_error();
                return false;
        }
        
        for (size_t i = 0; p[i] != NULL; ++i) {
                eval_statement(global_env, p[i]);
        }

        return true;
}

bool
interpreter_eval_source(char const *source, struct value *out)
{
        struct token *ts = lex(source);
        if (ts == NULL) {
                err_msg = lex_error();
                return false;
        }

        struct expression *e = parse_expression(ts);
        if (e == NULL) {
                err_msg = parse_error();
                return false;
        }

        if (setjmp(jb) != 0) {
                err_msg = eval_error();
                return false;
        }
        
        *out = eval_expression(global_env, e);

        return true;
}

char const *
interpreter_error(void)
{
        return err_msg;
}

// only used for testing small programs which are part of the test suite
bool
interpreter_execute_file(char const *path)
{
                return interpreter_execute_source(slurp(path));
}

TEST(integer)
{
        interpreter_init();

        struct expression e = { .type = EXPRESSION_INTEGER, .integer = 18 };
        struct value v = eval_expression(global_env, &e);

        claim(v.type == VALUE_INTEGER);
        claim(v.integer == e.integer);
}

TEST(object)
{
        char const *source = "({'hello':'world'});";

        struct token *ts = lex(source);
        claim(ts != NULL);

        struct statement **p = parse(ts);
        claim(p != NULL);

        struct statement *s = p[0];
        claim(s->type == STATEMENT_EXPRESSION);
        claim(s->expression->type == EXPRESSION_OBJECT);

        struct value v = eval_expression(global_env, s->expression);
        claim(v.type == VALUE_OBJECT);

        struct value hello = { .type = VALUE_STRING, .string = "hello" };  

        struct value *world = object_get_value(v.object, &hello);
        claim(world != NULL);
        claim(world->type == VALUE_STRING);
        claim(strcmp(world->string, "world") == 0);
}

TEST(definition)
{
        char const *source = "let [a, b] = [12, 13];";

        struct token *ts = lex(source);
        claim(ts != NULL);

        struct statement **p = parse(ts);
        claim(p != NULL);

        struct statement *s = p[0];
        claim(s->type == STATEMENT_DEFINITION);

        eval_statement(global_env, s);

        struct value *v = env_lookup(global_env, "a");
        claim(v != NULL);
        claim(v->type == VALUE_INTEGER);
        claim(v->integer == 12);

        v = env_lookup(global_env, "b");
        claim(v != NULL);
        claim(v->type == VALUE_INTEGER);
        claim(v->integer == 13);
}

TEST(function_call)
{
        char const *source = "let f = function () { return 11; }; let k = f();";

        struct token *ts = lex(source);
        claim(ts != NULL);

        struct statement **p = parse(ts);
        claim(p != NULL);

        struct statement *s = p[0];
        claim(s->type == STATEMENT_DEFINITION);
        eval_statement(global_env, s);

        s = p[1];
        claim(s->type == STATEMENT_DEFINITION);
        eval_statement(global_env, s);

        struct value *v = env_lookup(global_env, "k");
        claim(v != NULL);
        claim(v->type == VALUE_INTEGER);
        claim(v->integer == 11);

}

TEST(method_call)
{
        char const *source = "let p = {'name': 'Alice', 'getName': function (self) { return self.name; }}; let name = p.getName();";

        struct token *ts = lex(source);
        claim(ts != NULL);

        struct statement **p = parse(ts);
        claim(p != NULL);

        struct statement *s = p[0];
        claim(s->type == STATEMENT_DEFINITION);
        eval_statement(global_env, s);

        s = p[1];
        claim(s->type == STATEMENT_DEFINITION);
        eval_statement(global_env, s);

        struct value *v = env_lookup(global_env, "name");
        claim(v != NULL);
        claim(v->type == VALUE_STRING);
        claim(strcmp(v->string, "Alice") == 0);

}

TEST(programs)
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
                printf("\nRunning program: %s\n", test_progs[i]);
                interpreter_init();
                for (struct statement **s = program; *s != NULL; ++s) {
                        eval_statement(global_env, *s);
                }
                struct value *v = env_lookup(global_env, "result");
                claim(v != NULL);
                claim(v->type == VALUE_INTEGER);
                claim(v->integer == 42);
        }
}

