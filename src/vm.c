#include <string.h>
#include <assert.h>
#include <stdbool.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdnoreturn.h>

#include "vm.h"
#include "gc.h"
#include "object.h"
#include "value.h"
#include "alloc.h"
#include "compiler.h"
#include "test.h"
#include "log.h"
#include "operators.h"
#include "builtin_functions.h"
#include "array_methods.h"
#include "string_methods.h"

#define READVALUE(s) (memcpy(&s, ip, sizeof s), (ip += sizeof s))
#define CASE(i)      case INSTR_ ## i: LOG("executing instr: " #i);

struct variable {
        bool mark;
        bool captured;
        struct variable *prev;
        struct value value;
        struct variable *next;
};

/*
 * Linked-list of captured variables which is traversed during garbage-collection.
 */
static struct variable *captured_chain;

static jmp_buf jb;

static struct variable **vars;
static vec(struct value) stack;
static vec(char *) callstack;
static vec(struct value *) targetstack;

static int symbolcount = 0;

static char const *err_msg;
static char err_buf[8192];

static struct value nil = { .type = VALUE_NIL };

static struct {
        char const *name;
        struct value (*fn)(value_vector *);
} builtins[] = {
        { .name = "print", .fn = builtin_print }
};

static int builtin_count = sizeof builtins / sizeof builtins[0];

static struct variable *
newvar(struct variable *next)
{
        struct variable *v = alloc(sizeof *v);
        v->mark = true;
        v->captured = false;
        v->prev = NULL;
        v->next = next;

        return v;
}

static void
add_builtins(void)
{
        resize(vars, sizeof *vars * builtin_count);
        for (int i = 0; i < builtin_count; ++i) {
                vars[i] = newvar(NULL);
        }

        for (int i = 0; i < builtin_count; ++i) {
                compiler_introduce_symbol(builtins[i].name);
                vars[i]->value = (struct value){ .type = VALUE_BUILTIN_FUNCTION, .builtin_function = builtins[i].fn };
        }

        symbolcount = builtin_count;
}

inline static struct value
pop(void)
{
        return *vec_pop(stack);
}

inline static struct value
peek(void)
{
        return stack.items[stack.count - 1];
}

inline static struct value *
top(void)
{
        return &stack.items[stack.count - 1];
}

inline static void
push(struct value v)
{
        LOG("pushing %s", value_show(&v));
        vec_push(stack, v);
}

inline static struct value *
poptarget(void)
{
        return *vec_pop(targetstack);
}

inline static struct value *
peektarget(void)
{
        return targetstack.items[targetstack.count - 1];
}

inline static void
pushtarget(struct value *v)
{
        vec_push(targetstack, v);
}

static void
vm_exec(char *code)
{
        char *ip = code;

        uintptr_t s, off;
        intmax_t k;
        bool b;
        float f;
        int n;

        struct value left, right, v, key, value, container, subscript, *vp;

        value_vector args;
        struct value (*func)(struct value *, value_vector *);

        struct variable *next;

        for (;;) {
                switch (*ip++) {
                CASE(PUSH_VAR)
                        READVALUE(s);
                        vars[s] = newvar(vars[s]);
                        break;
                CASE(POP_VAR)
                        READVALUE(s);
                        next = vars[s]->next;
                        if (vars[s]->captured) {
                                LOG("detaching captured variable");
                                vars[s]->next->prev = NULL;
                                vars[s]->next = captured_chain;
                                captured_chain = vars[s];
                        }
                        vars[s] = next;
                        break;
                CASE(LOAD_VAR)
                        READVALUE(s);
                        LOG("loading %d", (int) s);
                        push(vars[s]->value);
                        break;
                CASE(EXEC_CODE)
                        READVALUE(s);
                        vec_push(callstack, ip);
                        ip = (char *) s;
                        break;
                CASE(DUP)
                        push(peek());
                        break;
                CASE(JUMP)
                        READVALUE(n);
                        LOG("JUMPING %d", n);
                        ip += n;
                        break;
                CASE(COND_JUMP)
                        READVALUE(n);
                        if (peek().type != VALUE_BOOLEAN) {
                                vm_panic("non-boolean used as condition");
                        }
                        if (pop().boolean) {
                                LOG("JUMPING %d", n);
                                ip += n;
                        }
                        break;
                CASE(NCOND_JUMP)
                        READVALUE(n);
                        if (peek().type != VALUE_BOOLEAN) {
                                vm_panic("non-boolean used as condition");
                        }
                        if (!pop().boolean) {
                                LOG("JUMPING %d", n);
                                ip += n;
                        }
                        break;
                CASE(TARGET_VAR)
                        READVALUE(s);
                        LOG("targetting %d", (int) s);
                        pushtarget(&vars[s]->value);
                        break;
                CASE(TARGET_REF)
                        READVALUE(s);
                        LOG("ref = %p", (void *) s);
                        pushtarget(&((struct variable *) s)->value);
                        break;
                CASE(TARGET_MEMBER)
                        v = pop();
                        if (v.type != VALUE_OBJECT) {
                                vm_panic("assignment to member of non-object");
                        }
                        pushtarget(object_put_member_if_not_exists(v.object, ip));
                        ip += strlen(ip) + 1;
                        break;
                CASE(TARGET_SUBSCRIPT)
                        subscript = pop();
                        container = pop();

                        if (container.type == VALUE_ARRAY) {
                                if (subscript.type != VALUE_INTEGER) {
                                        vm_panic("non-integer array index used in subscript assignment");
                                }
                                if (subscript.integer < 0) {
                                        subscript.integer += container.array->count;
                                }
                                if (subscript.integer < 0 || subscript.integer >= container.array->count) {
                                        vm_panic("array index out of range in subscript expression");
                                }
                                pushtarget(&container.array->items[subscript.integer]);
                        } else if (container.type == VALUE_OBJECT) {
                                pushtarget(object_put_key_if_not_exists(container.object, subscript));
                        } else {
                                vm_panic("attempt to perform subscript assignment on something other than an object or array");
                        }
                        break;
                CASE(ASSIGN)
                        *poptarget() = peek();
                        break;
                CASE(POP)
                        pop();
                        break;
                CASE(LOAD_REF)
                        READVALUE(s);
                        LOG("reference is: %p", (void *) s);
                        push(((struct variable *) s)->value);
                        break;
                CASE(INTEGER)
                        READVALUE(k);
                        push((struct value){ .type = VALUE_INTEGER, .integer = k });
                        break;
                CASE(REAL)
                        READVALUE(f);
                        push((struct value){ .type = VALUE_REAL, .real = f });
                        break;
                CASE(BOOLEAN)
                        READVALUE(b);
                        push((struct value){ .type = VALUE_BOOLEAN, .boolean = b });
                        break;
                CASE(STRING)
                        push((struct value){ .type = VALUE_STRING, .string = ip });
                        ip += strlen(ip) + 1;
                        break;
                CASE(ARRAY)
                        v.type = VALUE_ARRAY;
                        v.array = value_array_new();

                        READVALUE(n);
                        vec_reserve(*v.array, n);
                        for (int i = 0; i < n; ++i) {
                                vec_push(*v.array, pop());
                        }

                        push(v);
                        break;
                CASE(OBJECT)
                        v.type = VALUE_OBJECT;
                        v.object = object_new();

                        READVALUE(n);
                        for (int i = 0; i < n; ++i) {
                                value = pop();
                                key = pop();
                                object_put_value(v.object, key, value);
                        }

                        push(v);
                        break;
                CASE(NIL)
                        push(nil);
                        break;
                CASE(MEMBER_ACCESS)
                        v = pop();
                        if (v.type != VALUE_OBJECT) {
                                vm_panic("member access on non-object");
                        }
                        vp = object_get_member(v.object, ip);
                        ip += strlen(ip) + 1;

                        push((vp == NULL) ? nil : *vp);
                        break;
                CASE(SUBSCRIPT)
                        subscript = pop();
                        container = pop();

                        if (container.type == VALUE_ARRAY) {
                                if (subscript.type != VALUE_INTEGER) {
                                        vm_panic("non-integer array index used in subscript expression");
                                }
                                if (subscript.integer < 0) {
                                        subscript.integer += container.array->count;
                                }
                                if (subscript.integer < 0 || subscript.integer >= container.array->count) {
                                        vm_panic("array index out of range in subscript expression");
                                }
                                push(container.array->items[subscript.integer]);
                        } else if (container.type == VALUE_OBJECT) {
                                vp = object_get_value(container.object, &subscript);
                                push((vp == NULL) ? nil : *vp);
                        } else {
                                vm_panic("attempt to subscript something other than an object or array");
                        }
                        break;
                CASE(NOT)
                        v = pop();
                        push(unary_operator_not(&v));
                        break;
                CASE(NEG)
                        v = pop();
                        push(unary_operator_negate(&v));
                        break;
                CASE(ADD)
                        right = pop();
                        left = pop();
                        push(binary_operator_addition(&left, &right));
                        break;
                CASE(SUB)
                        right = pop();
                        left = pop();
                        push(binary_operator_subtraction(&left, &right));
                        break;
                CASE(MUL)
                        right = pop();
                        left = pop();
                        push(binary_operator_multiplication(&left, &right));
                        break;
                CASE(DIV)
                        right = pop();
                        left = pop();
                        push(binary_operator_division(&left, &right));
                        break;
                CASE(MOD)
                        right = pop();
                        left = pop();
                        push(binary_operator_remainder(&left, &right));
                        break;
                CASE(EQ)
                        right = pop();
                        left = pop();
                        push(binary_operator_equality(&left, &right));
                        break;
                CASE(LT)
                        right = pop();
                        left = pop();
                        push(binary_operator_less_than(&left, &right));
                        break;
                CASE(GT)
                        right = pop();
                        left = pop();
                        push(binary_operator_greater_than(&left, &right));
                        break;
                CASE(LEQ)
                        right = pop();
                        left = pop();
                        push(binary_operator_less_than_or_equal(&left, &right));
                        break;
                CASE(GEQ)
                        right = pop();
                        left = pop();
                        push(binary_operator_greater_than_or_equal(&left, &right));
                        break;
                CASE(KEYS)
                        v = pop();
                        push(unary_operator_keys(&v));
                        break;
                CASE(LEN)
                        v = pop();
                        push((struct value){ .type = VALUE_INTEGER, .integer = v.array->count }); // TODO
                        break;
                CASE(INC) // only used for internal (hidden) variables
                        READVALUE(s);
                        ++vars[s]->value.integer;
                        break;
                CASE(PRE_INC)
                        ++peektarget()->integer;
                        push(*poptarget());
                        break;
                CASE(POST_INC)
                        push(*peektarget());
                        ++poptarget()->integer;
                        break;
                CASE(PRE_DEC)
                        --peektarget()->integer;
                        push(*poptarget());
                        break;
                CASE(POST_DEC)
                        push(*peektarget());
                        --poptarget()->integer;
                        break;
                CASE(FUNCTION)
                        v.type = VALUE_FUNCTION;

                        READVALUE(n);
                        vec_init(v.bound_symbols);
                        vec_reserve(v.bound_symbols, n);
                        LOG("function has %d bound symbol(s)", n);
                        for (int i = 0; i < n; ++i) {
                                READVALUE(s);
                                vec_push(v.bound_symbols, s);
                        }

                        READVALUE(n);
                        v.code = ip;
                        ip += n;

                        READVALUE(n);
                        vec_init(v.param_symbols);
                        vec_reserve(v.param_symbols, n);
                        LOG("function has %d parameter(s)", n);
                        for (int i = 0; i < n; ++i) {
                                READVALUE(s);
                                vec_push(v.param_symbols, s);
                        }

                        READVALUE(n);
                        v.refs = ref_vector_new(n);
                        LOG("function contains %d reference(s)", n);
                        for (int i = 0; i < n; ++i) {
                                READVALUE(s);
                                READVALUE(off);
                                vars[s]->captured = true;
                                struct reference ref = { .pointer = (uintptr_t) vars[s], .offset = off };
                                LOG("it refers to symbol %d", (int) s);
                                LOG("it refers to pointer %p", (void *) ref.pointer);
                                v.refs->refs[i] = ref;
                        }

                        push(v);
                        break;
                CASE(CALL)
                        v = pop();
                        if (v.type == VALUE_FUNCTION) {
                                for (int i = 0; i < v.bound_symbols.count; ++i) {
                                        if (vars[v.bound_symbols.items[i]] == NULL) {
                                                vars[v.bound_symbols.items[i]] = newvar(NULL);
                                        }
                                        if (vars[v.bound_symbols.items[i]]->prev == NULL) {
                                                vars[v.bound_symbols.items[i]]->prev = newvar(vars[v.bound_symbols.items[i]]);
                                        }
                                        vars[v.bound_symbols.items[i]] = vars[v.bound_symbols.items[i]]->prev;
                                }
                                READVALUE(n);
                                while (n > v.param_symbols.count) {
                                        pop();
                                        --n;
                                }
                                for (int i = n; i < v.param_symbols.count; ++i) {
                                        vars[v.param_symbols.items[i]]->value = nil;
                                }
                                while (n --> 0) {
                                        vars[v.param_symbols.items[n]]->value = pop();
                                }
                                for (int i = 0; i < v.refs->count; ++i) {
                                        struct reference ref = v.refs->refs[i];
                                        LOG("resolving reference to %p", (void *) ref.pointer);
                                        memcpy(v.code + ref.offset, &ref.pointer, sizeof ref.pointer);
                                }
                                vec_push(callstack, ip);
                                assert(v.code != NULL);
                                ip = v.code;
                        } else if (v.type == VALUE_BUILTIN_FUNCTION) {
                                vec_init(args);
                                READVALUE(n);
                                while (n --> 0) {
                                        vec_push(args, pop());
                                }
                                push(v.builtin_function(&args));
                        } else {
                                vm_panic("attempt to call a non-function");
                        }
                        break;
                CASE(CALL_METHOD)
                        v = pop();

                        if (v.type == VALUE_STRING) {
                                func = get_string_method(ip);
                                if (func == NULL) {
                                        vm_panic("call to non-existant string method: %s", ip);
                                }
                                ip += strlen(ip) + 1;
                                vec_init(args);
                                READVALUE(n);
                                while (n --> 0) {
                                        vec_push(args, pop());
                                }
                                push(func(&v, &args));
                                break;
                        }

                        if (v.type == VALUE_ARRAY) {
                                func = get_array_method(ip);
                                if (func == NULL) {
                                        vm_panic("call to non-existant array method: %s", ip);
                                }
                                ip += strlen(ip) + 1;
                                vec_init(args);
                                READVALUE(n);
                                while (n --> 0) {
                                        vec_push(args, pop());
                                }
                                push(func(&v, &args));
                                break;
                        }

                        if (v.type != VALUE_OBJECT) {
                                vm_panic("attempt to call a method on a non-object");
                        }

                        vp = object_get_member(v.object, ip);
                        if (vp == NULL) {
                                vm_panic("attempt to call a non-existant method: %s", ip);
                        }

                        if (vp->type != VALUE_FUNCTION) {
                                vm_panic("attempt to call a non-function as a method on an object: %s", ip);
                        }
                        ip += strlen(ip) + 1;

                        for (int i = 0; i < vp->bound_symbols.count; ++i) {
                                vars[vp->bound_symbols.items[i]] = newvar(vars[vp->bound_symbols.items[i]]);
                        }
                        READVALUE(n);
                        if (vp->param_symbols.count >= 1) {
                                vars[vp->param_symbols.items[0]]->value = v;
                        }
                        while (n > 0 && n + 1 > vp->param_symbols.count) {
                                pop();
                                --n;
                        }
                        for (int i = n + 1; i < vp->param_symbols.count; ++i) {
                                vars[vp->param_symbols.items[i]]->value = nil;
                        }
                        while (n --> 0) {
                                vars[vp->param_symbols.items[n + 1]]->value = pop();
                        }
                        vec_push(callstack, ip);
                        assert(vp->code != NULL);
                        ip = vp->code;
                        break;
                CASE(RETURN)
                        ip = *vec_pop(callstack);
                        break;
                CASE(HALT)
                        if (callstack.count == 0) {
                                return;
                        } else {
                                ip = *vec_pop(callstack);
                                break;
                        }
                }
        }
}

char const *
vm_error(void)
{
        return err_msg;
}

void
vm_init(void)
{
        vec_init(stack);
        vec_init(callstack);
        vec_init(targetstack);
        vars = NULL;
        symbolcount = 0;

        compiler_init();
        gc_reset();

        add_builtins();
}

noreturn void
vm_panic(char const *fmt, ...)
{
        va_list ap;
        va_start(ap, fmt);

        int n = sprintf(err_buf, "Runtime error: ");
        vsnprintf(err_buf + n, sizeof err_buf - n, fmt, ap);

        va_end(ap);

        longjmp(jb, 1);
}

bool
vm_execute(char const *source)
{
        int oldsymcount = symbolcount;

        LOG("EXECUTING SOURCE: '%s'", source);

        char *code = compiler_compile_source(source, &symbolcount);
        if (code == NULL) {
                err_msg = compiler_error();
                return false;
        }

        resize(vars, symbolcount * sizeof *vars);
        for (int i = oldsymcount; i < symbolcount; ++i) {
                LOG("SETTING %d TO NULL", i);
                vars[i] = NULL;
        }

        if (setjmp(jb) != 0) {
                vec_empty(stack);
                err_msg = err_buf;
                return false;
        }

        vm_exec(code);

        return true;
}

void
vm_mark(void)
{
        LOG("STARTING VM MARK");
        for (int i = 0; i < symbolcount; ++i) {
                LOG("  MARKING STACK %d", i);
                for (struct variable *v = vars[i]; v != NULL; v = v->next) {
                        value_mark(&v->value);
                }
                LOG("  DONE MARKING STACK %d", i);
        }
        LOG("MARKING TEMPORARY STACK");
        for (int i = 0; i < stack.count; ++i) {
                value_mark(&stack.items[i]);
        }
        LOG("DONE VM MARK");
}

void
vm_mark_variable(struct variable *v)
{
        v->mark = true;
        value_mark(&v->value);
}

void
vm_sweep_variables(void)
{
        while (captured_chain != NULL && !captured_chain->mark) {
                struct variable *next = captured_chain->next;
                free(captured_chain);
                captured_chain = next;
        }
        if (captured_chain != NULL) {
                captured_chain->mark = false;
        }
        for (struct variable *var = captured_chain; var != NULL && var->next != NULL;) {
                struct variable *next;
                if (!var->next->mark) {
                        next = var->next->next;
                        free(var->next);
                        var->next = next;
                } else {
                        next = var->next;
                }
                if (next != NULL) {
                        next->mark = false;
                }
                var = next;
        }
}

TEST(let)
{
        char const *source = "let a = 5;";

        vm_init();

        vm_execute(source);

        claim(vars[0 + builtin_count]->value.type == VALUE_INTEGER);
        claim(vars[0 + builtin_count]->value.integer == 5);
}

TEST(loop)
{
        char const *source = "let a = 0; for (let i = 0; i < 10; i = i + 1) a = a + 2;";

        vm_init();

        vm_execute(source);

        claim(vars[0 + builtin_count]->value.type == VALUE_INTEGER);
        LOG("value is %d", (int) vars[0 + builtin_count]->value.integer);
        claim(vars[0 + builtin_count]->value.integer == 20);
}

TEST(func)
{
        char const *source = "let a = 0; let f = function () { a = a + 1; }; f(); f();";

        vm_init();

        vm_execute(source);

        claim(vars[0 + builtin_count]->value.type == VALUE_INTEGER);
        LOG("value is %d", (int) vars[0 + builtin_count]->value.integer);
        claim(vars[0 + builtin_count]->value.integer == 2);
}

TEST(stress) // OFF
{
        char const *source = "let n = 0; for (let i = 0; i < 1000000; i = i + 1) { n = n + 1; }";

        vm_init();

        vm_execute(source);

        claim(vars[0 + builtin_count]->value.type == VALUE_INTEGER);
        LOG("value is %d", (int) vars[0 + builtin_count]->value.integer);
        claim(vars[0 + builtin_count]->value.integer == 1000000);
}

TEST(stress2) // OFF
{
        char const *source = "let n = 0; for (let i = 0; i < 1000000; i = i + 1) { n = n + (function () return 1;)(); }";

        vm_init();

        vm_execute(source);

        claim(vars[0 + builtin_count]->value.type == VALUE_INTEGER);
        LOG("value is %d", (int) vars[0 + builtin_count]->value.integer);
        claim(vars[0 + builtin_count]->value.integer == 1000000);
}

TEST(array)
{
        char const *source = "let a = [1, 2 + 2, 16];";

        vm_init();

        vm_execute(source);

        claim(vars[0 + builtin_count]->value.type == VALUE_ARRAY);
        claim(vars[0 + builtin_count]->value.array->count == 3);
        claim(vars[0 + builtin_count]->value.array->items[0].type == VALUE_INTEGER);
        claim(vars[0 + builtin_count]->value.array->items[1].type == VALUE_INTEGER);
        claim(vars[0 + builtin_count]->value.array->items[2].type == VALUE_INTEGER);
        claim(vars[0 + builtin_count]->value.array->items[0].integer == 1);
        claim(vars[0 + builtin_count]->value.array->items[1].integer == 4);
        claim(vars[0 + builtin_count]->value.array->items[2].integer == 16);
}

TEST(object)
{
        char const *source = "let o = {'test': 'hello'};";

        vm_init();

        vm_execute(source);

        claim(vars[0 + builtin_count]->value.type == VALUE_OBJECT);
        struct value *v = object_get_member(vars[0 + builtin_count]->value.object, "test");
        claim(v != NULL);
        claim(strcmp(v->string, "hello") == 0);
}

TEST(member_access)
{
        char const *source = "let o = {'test': 'hello'}; let h = o.test;";

        vm_init();

        vm_execute(source);

        claim(vars[1 + builtin_count]->value.type == VALUE_STRING);
        claim(strcmp(vars[1 + builtin_count]->value.string, "hello") == 0);
}

TEST(subscript)
{
        char const *source = "let o = {'test': 'hello'}; let h = o['test'];";

        vm_init();

        vm_execute(source);

        claim(vars[1 + builtin_count]->value.type == VALUE_STRING);
        claim(strcmp(vars[1 + builtin_count]->value.string, "hello") == 0);
}

TEST(array_lvalue)
{
        char const *source = "let [a, [b, c]] = [4, [10, 16]];";

        vm_init();

        vm_execute(source);

        claim(vars[0 + builtin_count]->value.type == VALUE_INTEGER);
        claim(vars[0 + builtin_count]->value.integer == 4);

        claim(vars[1 + builtin_count]->value.type == VALUE_INTEGER);
        claim(vars[1 + builtin_count]->value.integer == 10);

        claim(vars[2 + builtin_count]->value.type == VALUE_INTEGER);
        claim(vars[2 + builtin_count]->value.integer == 16);
}

TEST(array_subscript)
{
        char const *source = "let a = [4, 5, 6]; a[0] = 42; let b = a[0];";

        vm_init();

        vm_execute(source);

        claim(vars[1 + builtin_count]->value.type == VALUE_INTEGER);
        claim(vars[1 + builtin_count]->value.integer == 42);
}

TEST(func_with_args)
{
        char const *source = "let a = 0; let f = function (k) { return k + 10; }; a = f(32);";

        vm_init();

        vm_execute(source);

        claim(vars[0 + builtin_count]->value.type == VALUE_INTEGER);
        claim(vars[0 + builtin_count]->value.integer == 42);
}

TEST(if_else)
{
        char const *source = "let [a, b] = [nil, nil]; if (false) { a = 48; } else { a = 42; } if (true) { b = 42; } else { b = 98; }";

        vm_init();

        vm_execute(source);

        claim(vars[0 + builtin_count]->value.type == VALUE_INTEGER);
        claim(vars[0 + builtin_count]->value.integer == 42);

        claim(vars[1 + builtin_count]->value.type == VALUE_INTEGER);
        claim(vars[1 + builtin_count]->value.integer == 42);
}

TEST(recursive_func)
{
        char const *source = "let a = 0; let f = function (k) if (k == 1) return 1; else return k * f(k - 1);; a = f(5);";

        vm_init();

        vm_execute(source);

        claim(vars[0 + builtin_count]->value.type == VALUE_INTEGER);
        LOG("a = %d", (int) vars[0 + builtin_count]->value.integer);
        claim(vars[0 + builtin_count]->value.integer == 120);
}

TEST(method_call)
{
        char const *source = "let o = {'name': 'foobar', 'getName': function (self) { return self.name; }}; o = o.getName();";

        vm_init();

        vm_execute(source);

        claim(vars[0 + builtin_count]->value.type == VALUE_STRING);
        claim(strcmp(vars[0 + builtin_count]->value.string, "foobar") == 0);
}

TEST(print)
{
        vm_init();
        vm_execute("print(45);");
}


TEST(each)
{
        vm_init();
        claim(vm_execute("let o = { 'name': 'Bob', 'age':  19 };"));
        claim(vm_execute("for (k in @o) { print(k); print(o[k]); print('---'); }"));
}

TEST(bench)
{
        vm_init();
        vm_execute("for (let i = 0; i < 1000; i = i + 1) { let [a, b, c] = [{}, {}, {}]; }");
}

TEST(factorial)
{
        vm_init();

        vm_execute("let f = function (k) if (k == 1) return 1; else return k * f(k - 1);;");
        vm_execute("f(5);");
}
