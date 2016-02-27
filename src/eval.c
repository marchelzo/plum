#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <inttypes.h>
#include <assert.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdnoreturn.h>

#include "log.h"
#include "alloc.h"
#include "util.h"
#include "eval.h"
#include "environment.h"
#include "value.h"
#include "ast.h"
#include "object.h"
#include "builtin_functions.h"
#include "lex.h"
#include "parse.h"

typedef vec(char *) string_vector;

static struct value
eval(struct environment *env, struct expression const *e);

static void
execute(struct environment *env, struct statement const *s);

static void
do_assignment(struct environment *env, struct lvalue const *target, struct value v);

static void
do_definition(struct environment *env, struct lvalue const *target, struct value v);

static void
expression_free_variables(struct expression const *e, struct environment *env, string_vector *vec);

static void
statement_free_variables(struct statement const *e, struct environment *env, string_vector *vec);

static jmp_buf *function_call_jb = NULL;

static void (*panic_hook)(void) = NULL;

static struct value const nil = {
        .type = VALUE_NIL
};

static struct value function_return_value = { .type = VALUE_NIL };

static char err_buf[8192];

noreturn void
eval_panic(char const *fmt, ...)
{
        assert(panic_hook != NULL);
        
        va_list ap;
        va_start(ap, fmt);

        vsnprintf(err_buf, sizeof err_buf, fmt, ap);

        va_end(ap);

        panic_hook();
}

static void
lvalue_free_variables(struct lvalue const *lv, struct environment *env, string_vector *vec)
{
        switch (lv->type) {
        case LVALUE_ARRAY:
                for (size_t i = 0; i < lv->lvalues.count; ++i) {
                        lvalue_free_variables(lv->lvalues.items[i], env, vec);
                }
                break;
        case LVALUE_SUBSCRIPT:
                expression_free_variables(lv->subscript, env, vec);
                break;
        case LVALUE_MEMBER:
                expression_free_variables(lv->object, env, vec);
                break;
        }
}

static void
expression_free_variables(struct expression const *e, struct environment *env, string_vector *vec)
{
        if (e == NULL) {
                return;
        }

        switch (e->type) {
        case EXPRESSION_VARIABLE:
                if (env_lookup(env, e->identifier) == NULL) {
                        vec_push(*vec, e->identifier);
                }
                break;
        case EXPRESSION_ASSIGNMENT:
                lvalue_free_variables(e->target, env, vec);
                expression_free_variables(e->value, env, vec);
                break;
        case EXPRESSION_MEMBER_ACCESS:
                expression_free_variables(e->object, env, vec);
                break;
        case EXPRESSION_OBJECT:
                for (size_t i = 0; i < e->keys.count; ++i) {
                        expression_free_variables(e->keys.items[i], env, vec);
                        expression_free_variables(e->values.items[i], env, vec);
                }
                break;
        case EXPRESSION_ARRAY:
                for (size_t i = 0; i < e->elements.count; ++i) {
                        expression_free_variables(e->elements.items[i], env, vec);
                }
                break;
        case EXPRESSION_METHOD_CALL:
                expression_free_variables(e->object, env, vec);
                for (size_t i = 0; i < e->method_args.count; ++i) {
                        expression_free_variables(e->method_args.items[i], env, vec);
                }
        case EXPRESSION_FUNCTION_CALL:
                expression_free_variables(e->function, env, vec);
                for (size_t i = 0; i < e->args.count; ++i) {
                        expression_free_variables(e->args.items[i], env, vec);
                }
                break;
        case EXPRESSION_FUNCTION:
                env = env_new(env);
                for (size_t i = 0; i < e->params.count; ++i) {
                        env_insert(env, e->params.items[i], nil);
                }
                statement_free_variables(e->body, env, vec);
                break;
        }
}

static void
add_identifiers_to_env(struct lvalue const *target, struct environment *env)
{
        switch (target->type) {
        case LVALUE_NAME:
                env_insert(env, target->name, nil);
                break;
        case LVALUE_ARRAY:
                for (size_t i = 0; i < target->lvalues.count; ++i) {
                        add_identifiers_to_env(target->lvalues.items[i], env);
                }
                break;
        default:
                // TODO: error
                break;
        }
}

static void
statement_free_variables(struct statement const *s, struct environment *env, string_vector *vec)
{
        if (s == NULL) {
                return;
        }

        switch (s->type) {
        case STATEMENT_DEFINITION:
                expression_free_variables(s->value, env, vec);
                add_identifiers_to_env(s->target, env);
                break;
        case STATEMENT_EXPRESSION:
                expression_free_variables(s->expression, env, vec);
                break;
        case STATEMENT_CONDITIONAL:
                expression_free_variables(s->conditional.cond, env, vec);
                statement_free_variables(s->conditional.then_branch, env, vec);
                statement_free_variables(s->conditional.else_branch, env, vec);
                break;
        case STATEMENT_FOR_LOOP:
                statement_free_variables(s->for_loop.init, env, vec);
                expression_free_variables(s->for_loop.cond, env, vec);
                expression_free_variables(s->for_loop.next, env, vec);
                statement_free_variables(s->for_loop.body, env, vec);
                break;
        case STATEMENT_WHILE_LOOP:
                expression_free_variables(s->while_loop.cond, env, vec);
                statement_free_variables(s->while_loop.body, env, vec);
                break;
        case STATEMENT_BLOCK:
                env = env_new(env);
                for (size_t i = 0; i < s->statements.count; ++i) {
                        statement_free_variables(s->statements.items[i], env, vec);
                }
                break;
        }
}

inline static struct value
eval_integer(intmax_t k)
{
        return (struct value) {
                .type = VALUE_INTEGER,
                .integer = k
        };
}

inline static struct value
eval_real(float real)
{
        return (struct value) {
                .type = VALUE_REAL,
                .real = real
        };
}

inline static struct value
eval_boolean(bool boolean)
{
        return (struct value) {
                .type = VALUE_BOOLEAN,
                .boolean = boolean
        };
}

inline static struct value
eval_string(char const *s)
{
        return (struct value) {
                .type = VALUE_STRING,
                .string = s
        };
}

static struct value
eval_variable(struct environment *env, char const *name)
{
        struct value *v = env_lookup(env, name);
        if (v == NULL) {
                eval_panic("reference to undefined variable: %s", name);
        } else {
                return *v;
        }
}

static struct value
eval_array(struct environment *env, struct expression **es, size_t n)
{
        struct value array;

        array.type = VALUE_ARRAY;
        array.array = alloc(sizeof *array.array);
        vec_init(*array.array);

        for (size_t i = 0; i < n; ++i) {
                vec_push(*array.array, eval(env, es[i]));
        }

        return array;
}

static struct value
eval_object(struct environment *env, struct expression const *obj)
{
        struct value object;
        object.type = VALUE_OBJECT;
        object.object = object_new();

        assert(obj->type == EXPRESSION_OBJECT);
        assert(obj->keys.count == obj->values.count);

        for (int i = 0; i < obj->keys.count; ++i) {
                struct value key = eval(env, obj->keys.items[i]);
                struct value value = eval(env, obj->values.items[i]);
                object_put_value(object.object, key, value);
        }

        return object;
}

static struct value
eval_function(struct environment *env, struct expression const *fn)
{
        struct value function;

        function.type = VALUE_FUNCTION;

        assert(fn->type == EXPRESSION_FUNCTION);
        // TODO: probably shouldn't access vector members directly like this
        function.params.items = fn->params.items;
        function.params.count = fn->params.count;

        string_vector free_vars;
        vec_init(free_vars);
        struct environment *tmp_env = env_new(NULL);
        for (size_t i = 0; i < fn->params.count; ++i) {
                env_insert(tmp_env, fn->params.items[i], nil);
        }

        statement_free_variables(fn->body, tmp_env, &free_vars);

        struct environment *func_env = env_new(env);
        for (size_t i = 0; i < free_vars.count; ++i) {
                env_insert(func_env, free_vars.items[i], *env_lookup(env, free_vars.items[i]));
        }

        function.env = func_env;
        function.body = fn->body;
        
        return function;
}

static struct value
eval_method_call(struct environment *env, struct expression const *e)
{
        struct value object = eval(env, e->object);
        if (object.type != VALUE_OBJECT) {
                eval_panic("attempt to call method on non-object");
        }

        LOG("getting method %s", e->method_name);
        struct value *method_ptr = object_get_member(object.object, e->method_name);
        LOG("done getting method");
        if (method_ptr == NULL) {
                eval_panic("attempt to call non-existant method '%s' on object", e->method_name);
        }
        struct value method = *method_ptr;
        if (method.type != VALUE_FUNCTION) {
                eval_panic("attempt to call non-function-typed member '%s' as a method", e->method_name);
        }

        struct environment *func_env = env_new(env);

        jmp_buf jb;
        jmp_buf *save_jb = function_call_jb;
        function_call_jb = &jb;
        if (setjmp(jb) != 0) {
                // TODO: free env
                function_call_jb = save_jb;
                return function_return_value;
        }

        if (method.params.count >= 1) {
                env_insert(func_env, method.params.items[0], object);
        }

        for (size_t i = 1; i < method.params.count; ++i) {
                if (i > e->method_args.count) {
                        env_insert(func_env, method.params.items[i], nil);
                } else {
                        struct value v = eval(env, e->method_args.items[i - 1]);
                        LOG("parameter %s will have value %s", method.params.items[i], value_show(&v));
                        env_insert(func_env, method.params.items[i], v);
                }
        }

        execute(func_env, method.body);

        // TODO: free func_env

        function_call_jb = save_jb;
        return nil;
}

static struct value
eval_function_call(struct environment *env, struct expression const *e)
{
        struct value function = eval(env, e->function);
        if (function.type == VALUE_BUILTIN_FUNCTION) {
                value_vector args;
                vec_init(args);
                for (size_t i = 0; i < e->args.count; ++i) {
                        vec_push(args, eval(env, e->args.items[i]));
                }
                return function.builtin_function(&args);
        } else if (function.type != VALUE_FUNCTION) {
                eval_panic("attempt to call a non-function-typed value as a function");
        }

        struct environment *func_env = env_new(function.env);

        jmp_buf jb;
        jmp_buf *save_jb = function_call_jb;
        function_call_jb = &jb;
        if (setjmp(jb) != 0) {
                // TODO: free env
                function_call_jb = save_jb;
                return function_return_value;
        }

        for (size_t i = 0; i < function.params.count; ++i) {
                if (i >= e->args.count) {
                        env_insert(func_env, function.params.items[i], nil);
                } else {
                        env_insert(func_env, function.params.items[i], eval(env, e->args.items[i]));
                }
        }

        execute(func_env, function.body);

        // TODO: free func_env

        function_call_jb = save_jb;
        return nil;
}

static struct value
eval_subscript(struct environment *env, struct expression const *e)
{
        struct value container = eval(env, e->container);
        struct value subscript = eval(env, e->subscript); 

        if (container.type == VALUE_ARRAY) {
                if (subscript.type != VALUE_INTEGER) {
                        eval_panic("attempt to use non-integer-typed value as array subscript");
                }
                if (container.array->count <= subscript.integer) {
                        eval_panic("array index out of range");
                }
                return container.array->items[subscript.integer];
        } else if (container.type == VALUE_OBJECT) {
                struct value *value = object_get_value(container.object, &subscript);
                if (value == NULL) {
                        return nil;
                } else {
                        return *value;
                }
        } else {
                eval_panic("attempt to subscript a value which is neither an array nor an object");
        }
}

static struct value
eval_member_access(struct environment *env, struct expression const *e)
{
        struct value object = eval(env, e->object); 
        if (object.type != VALUE_OBJECT) {
                eval_panic("attempt to access a member of a value which is not an object");
        }

        LOG("getting member");
        struct value *v = object_get_member(object.object, e->member_name);
        LOG("done getting member");

        if (v == NULL) {
                return nil;
        } else {
                return *v;
        }
}

static struct value
eval_binop(struct environment *env, struct expression const *e)
{
        return e->binop(env, e->left, e->right);
}

static void
assign_nil(struct environment *env, struct lvalue const *target)
{
        if (target->type == LVALUE_ARRAY) {
                for (size_t i = 0; i < target->lvalues.count; ++i) {
                        assign_nil(env, target->lvalues.items[i]);
                }
        } else {
                do_assignment(env, target, nil);
        }
}

static void
define_nil(struct environment *env, struct lvalue const *target)
{
        if (target->type == LVALUE_ARRAY) {
                for (size_t i = 0; i < target->lvalues.count; ++i) {
                        assign_nil(env, target->lvalues.items[i]);
                }
        } else {
                do_definition(env, target, nil);
        }
}

static void
do_assignment(struct environment *env, struct lvalue const *target, struct value value)
{
        if (target->type == LVALUE_NAME) {
                struct value *v = env_lookup(env, target->name);
                if (v == NULL) {
                        eval_panic("attempt to assign to undefined variable: %s", target->name);
                } else {
                        *v = value;
                }
        } else if (target->type == LVALUE_ARRAY) {
                if (value.type != VALUE_ARRAY) {
                        eval_panic("attempt to assign scalar value to an array-typed lvalue");
                }
                size_t i;
                for (i = 0; i < target->lvalues.count && i < value.array->count; ++i) {
                        do_assignment(env, target->lvalues.items[i], value.array->items[i]);
                }
                while (i < target->lvalues.count) {
                        assign_nil(env, target->lvalues.items[i++]);
                }
        } else if (target->type == LVALUE_MEMBER) {
                struct value object = eval(env, target->object);
                if (object.type != VALUE_OBJECT) {
                        eval_panic("attempt to assign to member on a value which is not an object");
                }
                LOG("getting member for assignment");
                struct value *v = object_get_member(object.object, target->member_name);
                LOG("done getting member for assignment");
                if (v == NULL) {
                        object_put_member(object.object, target->member_name, value);
                } else {
                        *v = value;
                }
        } else if (target->type == LVALUE_SUBSCRIPT) {
                struct value container = eval(env, target->container);
                struct value subscript = eval(env, target->subscript);
                if (container.type == VALUE_OBJECT) {
                        struct value *v = object_get_value(container.object, &subscript);
                        if (v == NULL) {
                                object_put_value(container.object, subscript, value);
                        } else {
                                *v = value;
                        }
                } else if (container.type == VALUE_ARRAY) {
                        if (subscript.type != VALUE_INTEGER) {
                                eval_panic("attempt to use non-integer-typed value as array subscript");
                        }
                        if (container.array->count <= subscript.integer) {
                                eval_panic("array index out of range");
                        }
                        container.array->items[subscript.integer] = value;
                } else {
                        eval_panic("attempt to subscript a value which is neither an array nor an object");
                }
        } else {
                // TODO: error
        }
}

inline static struct value
eval_assignment(struct environment *env, struct expression const *e)
{
        struct value v = eval(env, e->value);
        do_assignment(env, e->target, v);

        return v;
}

static void
do_definition(struct environment *env, struct lvalue const *target, struct value value)
{
        if (target->type == LVALUE_NAME) {
                struct value *v = env_lookup(env, target->name);
                if (v == NULL) {
                        env_insert(env, target->name, value);
                } else {
                        //eval_panic("attempt to redeclare a previously declared variable: %s", target->name);
                }
        } else if (target->type == LVALUE_ARRAY) {
                if (value.type != VALUE_ARRAY) {
                        eval_panic("attempt to assign scalar value to an array-typed lvalue");
                }
                size_t i;
                for (i = 0; i < target->lvalues.count && i < value.array->count; ++i) {
                        do_definition(env, target->lvalues.items[i], value.array->items[i]);
                }
                while (i < target->lvalues.count) {
                        define_nil(env, target->lvalues.items[i++]);
                }
        } else {
                eval_panic("unreachable");
        }
}

static void
execute_return(struct environment *env, struct statement const *s)
{
        if (function_call_jb == NULL) {
                eval_panic("attempt to return outside of the context of a function");
        }

        if (s->return_value == NULL) {
                function_return_value = nil;
        } else {
                struct value v = eval(env, s->return_value);
                function_return_value = v;
        }

        longjmp(*function_call_jb, 1);
}

inline static void
execute_conditional(struct environment *env, struct statement const *s)
{
        struct value cond = eval(env, s->conditional.cond);

        if (cond.type != VALUE_BOOLEAN) {
                eval_panic("attempt to use a non-boolean as the condition in an if statement");
        }

        if (cond.boolean) {
                execute(env, s->conditional.then_branch);
        } else if (s->conditional.else_branch != NULL) {
                execute(env, s->conditional.else_branch);
        }
}

inline static void
execute_block(struct environment *env, struct statement const *s)
{
        for (size_t i = 0; i < s->statements.count; ++i) {
                execute(env, s->statements.items[i]);
        }
}

static void
execute_for_loop(struct environment *env, struct statement const *s)
{
        struct environment *loop_env = env_new(env);

        execute(loop_env, s->for_loop.init);
        
        for (;;) {
                struct value v = s->for_loop.cond != NULL
                               ? eval(loop_env, s->for_loop.cond)
                               : (struct value) { .type = VALUE_BOOLEAN, .boolean = true };
                if (v.type != VALUE_BOOLEAN) {
                        eval_panic("attempt to use a non-boolean as the condition in a for loop");
                }
                if (!v.boolean) {
                        break;
                }
                execute(loop_env, s->for_loop.body);
                eval(loop_env, s->for_loop.next);
        }

        // TODO: maybe free loop env?
}

static void
execute(struct environment *env, struct statement const *s)
{
        switch (s->type) {
        case STATEMENT_EXPRESSION:  eval(env, s->expression);                           break;
        case STATEMENT_CONDITIONAL: execute_conditional(env, s);                        break;
        case STATEMENT_BLOCK:       execute_block(env, s);                              break;
        case STATEMENT_DEFINITION:  do_definition(env, s->target, eval(env, s->value)); break;
        case STATEMENT_RETURN:      execute_return(env, s);                             break;
        case STATEMENT_FOR_LOOP:    execute_for_loop(env, s);                           break;
        default:                                                                        break;
        }
}

static struct value
eval(struct environment *env, struct expression const *e)
{
        switch (e->type) {
        case EXPRESSION_VARIABLE:      return eval_variable(env, e->identifier);
        case EXPRESSION_ASSIGNMENT:    return eval_assignment(env, e);
        case EXPRESSION_INTEGER:       return eval_integer(e->integer);
        case EXPRESSION_STRING:        return eval_string(e->string);
        case EXPRESSION_BOOLEAN:       return eval_boolean(e->boolean);
        case EXPRESSION_REAL:          return eval_real(e->real);
        case EXPRESSION_ARRAY:         return eval_array(env, e->elements.items, e->elements.count);
        case EXPRESSION_OBJECT:        return eval_object(env, e);
        case EXPRESSION_FUNCTION:      return eval_function(env, e);
        case EXPRESSION_FUNCTION_CALL: return eval_function_call(env, e);
        case EXPRESSION_METHOD_CALL:   return eval_method_call(env, e);
        case EXPRESSION_MEMBER_ACCESS: return eval_member_access(env, e);
        case EXPRESSION_SUBSCRIPT:     return eval_subscript(env, e);
        case EXPRESSION_BINOP:         return e->binop(env, e->left, e->right);
        case EXPRESSION_UNOP:          return e->unop(env, e->operand);
        case EXPRESSION_NIL:           return nil;
        default:                       break;
        }
}

void
eval_statement(struct environment *env, struct statement const *s)
{
        execute(env, s);
}

struct value
eval_expression(struct environment *env, struct expression const *e)
{
        return eval(env, e);
}

char const *
eval_error(void)
{
        return sclone(err_buf);
}

void
eval_set_panic_hook(void (*f)(void))
{
        panic_hook = f;
}
