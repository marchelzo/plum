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
#include "value.h"
#include "ast.h"
#include "object.h"
#include "builtin_functions.h"
#include "test.h"
#include "lex.h"
#include "parse.h"
#include "vm.h"

#define emit_instr(i) LOG("emitting instr: %s", #i); _emit_instr(i)

#define PLACEHOLDER_JUMP(t, name) \
        emit_instr(t); \
        size_t name = state.code.count; \
        emit_int(0);

#define PATCH_JUMP(name) \
        jumpdistance = state.code.count - name - sizeof (int); \
        memcpy(state.code.items + name, &jumpdistance, sizeof jumpdistance); \

#define JUMP(loc) \
        emit_instr(INSTR_JUMP); \
        emit_int(loc - state.code.count - sizeof (int));

struct scope {
        bool function;
        vec(int) func_symbols;
        vec(int) symbols;
        vec(char const *) identifiers;
        struct scope *parent;
        struct scope *func;
};

struct module {
        char *path;
        char *code;
        struct scope *scope;
};

struct import {
        char *name;
        struct scope *scope;
};

typedef vec(struct import)    import_vector;
typedef vec(struct reference) reference_vector;
typedef vec(int)              symbol_vector;
typedef vec(size_t)           offset_vector;
typedef vec(char)             byte_vector;

/*
 * State which is local to a single compilation unit.
 */
struct state {
        byte_vector code;

        symbol_vector bound_symbols;
        reference_vector refs;
        
        offset_vector breaks;
        offset_vector continues;

        import_vector imports;

        struct scope *global;
};

static jmp_buf jb;
static char const *err_msg;
static char err_buf[512];

static int builtin_count;
static int symbol;
static int jumpdistance;
static vec(struct module) modules;
static struct state state;

static struct scope *global;
static int global_count;

static void
symbolize_statement(struct scope *scope, struct statement *s);

static void
symbolize_expression(struct scope *scope, struct expression *e);

static void
emit_statement(struct statement const *s);

static void
emit_expression(struct expression const *e);

static void
emit_assignment(struct expression *target, struct expression const *e, int i);

static void
import_module(char *name, char *as);

static struct value const nil = {
        .type = VALUE_NIL
};

noreturn static void
fail(char const *fmt, ...)
{
        va_list ap;
        va_start(ap, fmt);

        int n = sprintf(err_buf, "Compiler error: ");
        vsnprintf(err_buf + n, sizeof err_buf - n, fmt, ap);

        va_end(ap);

        longjmp(jb, 1);
}

inline static void
addref(int symbol)
{
        LOG("adding reference: %d", symbol);

        struct reference r = {
                .symbol = symbol,
                .offset = state.code.count
        };

        vec_push(state.refs, r);
}

inline static int
addsymbol(struct scope *scope, char const *name)
{
        assert(name != NULL);

        vec_push(scope->identifiers, name);
        vec_push(scope->symbols, symbol);
        if (scope->func != NULL) {
                vec_push(scope->func->func_symbols, symbol);
        }
        LOG("adding symbol: %s -> %d", name, symbol);
        return symbol++;
}

inline static int
tmpsymbol(int i)
{
        static char idbuf[8];
        assert(i <= 9999999 && i >= 0);

        sprintf(idbuf, "%d", i);

        for (int i = 0; i < state.global->identifiers.count; ++i) {
                if (strcmp(state.global->identifiers.items[i], idbuf) == 0) {
                        return state.global->symbols.items[i];
                }
        }

        vec_push(state.global->identifiers, sclone(idbuf));
        vec_push(state.global->symbols, symbol);

        return symbol++;
}

inline static bool
locallydefined(struct scope const *scope, char const *name)
{
        for (int i = 0; i < scope->identifiers.count; ++i) {
                if (strcmp(scope->identifiers.items[i], name) == 0) {
                        return true;
                }
        }

        return false;
}

inline static int
getsymbol(struct scope const *scope, char const *name, bool *local)
{
        *local = true;

        while (scope != NULL) {
                for (int i = 0; i < scope->identifiers.count; ++i) {
                        if (strcmp(scope->identifiers.items[i], name) == 0) {
                                return scope->symbols.items[i];
                        }
                }

                if (scope->function) {
                        *local = false;
                }

                scope = scope->parent;
        }

        fail("reference to undefined variable: %s", name);
}

static struct scope *
newscope(struct scope *parent, bool function)
{
        struct scope *s = alloc(sizeof *s);

        s->function = function;
        s->parent = parent;
        s->func = (function || parent == NULL) ? s : parent->func;

        vec_init(s->symbols);
        vec_init(s->identifiers);
        vec_init(s->func_symbols);

        return s;
}

static struct state
freshstate(void)
{
        struct state s;

        vec_init(s.code);

        vec_init(s.refs);
        vec_init(s.bound_symbols);

        vec_init(s.breaks);
        vec_init(s.continues);

        vec_init(s.imports);

        s.global = newscope(global, false);

        return s;
}

static struct scope *
get_import_scope(char const *name)
{
        LOG("LOOKING FOR MODULE: %s", name);

        for (int i = 0; i < state.imports.count; ++i) {
                if (strcmp(name, state.imports.items[i].name) == 0) {
                        LOG("Returning: %s == %s", name, state.imports.items[i].name);
                        return state.imports.items[i].scope;
                }
        }

        fail("reference to undefined module: %s", name);
}

static void
symbolize_lvalue(struct scope *scope, struct expression *target, bool define)
{
        switch (target->type) {
        case EXPRESSION_VARIABLE:
                if (define) {
                        if (locallydefined(scope, target->identifier)) {
                                fail("redeclaration of variable: %s", target->identifier);
                        }
                        target->symbol = addsymbol(scope, target->identifier);
                        target->local = true;
                } else {
                        target->symbol = getsymbol(scope, target->identifier, &target->local);
                }
                break;
        case EXPRESSION_ARRAY:
                for (size_t i = 0; i < target->elements.count; ++i) {
                        symbolize_lvalue(scope, target->elements.items[i], define);
                }
                break;
        case EXPRESSION_SUBSCRIPT:
                symbolize_expression(scope, target->container);
                symbolize_expression(scope, target->subscript);
                break;
        case EXPRESSION_MEMBER_ACCESS:
                symbolize_expression(scope, target->object);
                break;
        }
}

static void
symbolize_expression(struct scope *scope, struct expression *e)
{
        if (e == NULL) {
                return;
        }

        switch (e->type) {
        case EXPRESSION_VARIABLE:
                LOG("symbolizing var: %s", e->identifier);
                e->symbol = getsymbol(scope, e->identifier, &e->local);
                LOG("var %s local", e->local ? "is" : "is NOT");
                break;
        case EXPRESSION_MODULE_ACCESS:
                LOG("symbolizing var: %s::%s", e->module, e->identifier);
                e->type = EXPRESSION_VARIABLE;
                symbolize_expression(get_import_scope(e->module), e);
                break;
        case EXPRESSION_PLUS:
        case EXPRESSION_MINUS:
        case EXPRESSION_STAR:
        case EXPRESSION_DIV:
        case EXPRESSION_PERCENT:
        case EXPRESSION_AND:
        case EXPRESSION_OR:
        case EXPRESSION_LT:
        case EXPRESSION_LEQ:
        case EXPRESSION_GT:
        case EXPRESSION_GEQ:
        case EXPRESSION_DBL_EQ:
        case EXPRESSION_NOT_EQ:
                symbolize_expression(scope, e->left);
                symbolize_expression(scope, e->right);
                break;
        case EXPRESSION_PREFIX_BANG:
        case EXPRESSION_PREFIX_MINUS:
        case EXPRESSION_PREFIX_AT:
        case EXPRESSION_PREFIX_INC:
        case EXPRESSION_PREFIX_DEC:
        case EXPRESSION_POSTFIX_INC:
        case EXPRESSION_POSTFIX_DEC:
                symbolize_expression(scope, e->operand);
                break;
        case EXPRESSION_FUNCTION_CALL:
                symbolize_expression(scope, e->function);
                for (size_t i = 0;  i < e->args.count; ++i) {
                        symbolize_expression(scope, e->args.items[i]);
                }
                break;
        case EXPRESSION_SUBSCRIPT:
                symbolize_expression(scope, e->container);
                symbolize_expression(scope, e->subscript);
                break;
        case EXPRESSION_MEMBER_ACCESS:
                symbolize_expression(scope, e->object);
                break;
        case EXPRESSION_METHOD_CALL:
                symbolize_expression(scope, e->object);
                for (size_t i = 0;  i < e->method_args.count; ++i) {
                        symbolize_expression(scope, e->method_args.items[i]);
                }
                break;
        case EXPRESSION_ASSIGNMENT:
                symbolize_expression(scope, e->value);
                symbolize_lvalue(scope, e->target, false);
                break;
        case EXPRESSION_FUNCTION:
                vec_init(e->param_symbols);
                scope = newscope(scope, true);
                for (size_t i = 0; i < e->params.count; ++i) {
                        vec_push(e->param_symbols, addsymbol(scope, e->params.items[i]));
                }
                symbolize_statement(scope, e->body);

                e->bound_symbols.items = scope->func_symbols.items;
                e->bound_symbols.count = scope->func_symbols.count;

                break;
        case EXPRESSION_ARRAY:
                for (size_t i = 0; i < e->elements.count; ++i) {
                        symbolize_expression(scope, e->elements.items[i]);
                }
                break;
        case EXPRESSION_OBJECT:
                for (size_t i = 0; i < e->keys.count; ++i) {
                        symbolize_expression(scope, e->keys.items[i]);
                        symbolize_expression(scope, e->values.items[i]);
                }
                break;
        }
}

static void
symbolize_statement(struct scope *scope, struct statement *s)
{
        if (s == NULL) {
                return;
        }

        switch (s->type) {
        case STATEMENT_IMPORT:
                import_module(s->import.module, s->import.as);
                break;
        case STATEMENT_EXPRESSION:
                symbolize_expression(scope, s->expression);
                break;
        case STATEMENT_BLOCK:
                scope = newscope(scope, false);
                for (size_t i = 0; i < s->statements.count; ++i) {
                        symbolize_statement(scope, s->statements.items[i]);
                }
                break;
        case STATEMENT_FOR_LOOP:
                scope = newscope(scope, false);
                symbolize_statement(scope, s->for_loop.init);
                symbolize_expression(scope, s->for_loop.cond);
                symbolize_expression(scope, s->for_loop.next);
                symbolize_statement(scope, s->for_loop.body);
                break;
        case STATEMENT_EACH_LOOP:
                scope = newscope(scope, false);
                symbolize_lvalue(scope, s->each.target, true);
                symbolize_expression(scope, s->each.array);
                symbolize_statement(scope, s->each.body);
                break;
        case STATEMENT_WHILE_LOOP:
                symbolize_expression(scope, s->while_loop.cond);
                symbolize_statement(scope, s->while_loop.body);
                break;
        case STATEMENT_CONDITIONAL:
                symbolize_expression(scope, s->conditional.cond);
                symbolize_statement(scope, s->conditional.then_branch);
                symbolize_statement(scope, s->conditional.else_branch);
                break;
        case STATEMENT_RETURN:
                symbolize_expression(scope, s->return_value);
                break;
        case STATEMENT_DEFINITION:
                symbolize_lvalue(scope, s->target, true);
                symbolize_expression(scope, s->value);
                break;
        }
}

static void
patch_loop_jumps(size_t begin, size_t end)
{
        for (int i = 0; i < state.continues.count; ++i) {
                int distance = begin - state.continues.items[i] - sizeof (int);
                memcpy(state.code.items + state.continues.items[i], &distance, sizeof distance);
        }

        for (int i = 0; i < state.breaks.count; ++i) {
                int distance = end - state.breaks.items[i] - sizeof (int);
                memcpy(state.code.items + state.breaks.items[i], &distance, sizeof distance);
        }
}

inline static void
_emit_instr(char c)
{
        vec_push(state.code, c);
}

inline static void
emit_int(int k)
{
        LOG("emitting int: %d", k);
        char const *s = (char *) &k;
        for (int i = 0; i < sizeof (int); ++i) {
                vec_push(state.code, s[i]);
        }
}

inline static void
emit_symbol(uintptr_t sym)
{
        LOG("emitting symbol: %"PRIuPTR, sym);
        char const *s = (char *) &sym;
        for (int i = 0; i < sizeof (uintptr_t); ++i) {
                vec_push(state.code, s[i]);
        }
}

inline static void
emit_ptr(void const *p)
{
        
        char const *s = (char *) &p;
        for (int i = 0; i < sizeof (void *); ++i) {
                vec_push(state.code, s[i]);
        }
}

inline static void
emit_integer(intmax_t k)
{
        
        LOG("emitting integer: %"PRIiMAX, k);
        char const *s = (char *) &k;
        for (int i = 0; i < sizeof (intmax_t); ++i) {
                vec_push(state.code, s[i]);
        }
}

inline static void
emit_boolean(bool b)
{
        
        LOG("emitting boolean: %s", b ? "true" : "false");
        char const *s = (char *) &b;
        for (int i = 0; i < sizeof (bool); ++i) {
                vec_push(state.code, s[i]);
        }
}

inline static void
emit_float(float f)
{
        
        LOG("emitting float: %f", f);
        char const *s = (char *) &f;
        for (int i = 0; i < sizeof (float); ++i) {
                vec_push(state.code, s[i]);
        }
}

inline static void
emit_string(char const *s)
{
        
        LOG("emitting string: %s", s);
        size_t n = strlen(s) + 1;
        for (int i = 0; i < n; ++i) {
                vec_push(state.code, s[i]);
        }
}

static void
emit_function(struct expression const *e)
{
        assert(e->type == EXPRESSION_FUNCTION);
        
        /*
         * Save the current reference and bound-symbols vectors so we can
         * restore them after compiling the current function.
         */
        reference_vector refs_save = state.refs;
        symbol_vector syms_save = state.bound_symbols;
        vec_init(state.refs);
        state.bound_symbols.items = e->bound_symbols.items;
        state.bound_symbols.count = e->bound_symbols.count;


        emit_int(e->bound_symbols.count);
        for (int i = 0; i < e->bound_symbols.count; ++i) {
                LOG("bound sym: %d", e->bound_symbols.items[i]);
                emit_symbol(e->bound_symbols.items[i]);
        }

        /*
         * Write an int to the emitted code just to make some room.
         */
        size_t size_offset = state.code.count;
        emit_int(0);

        /*
         * Remember where in the code this function's code begins so that we can compute
         * the relative offset of references to non-local variables.
         */
        size_t start_offset = state.code.count;

        emit_statement(e->body);

        /*
         * Add an implicit 'return nil;' in case the function doesn't explicitly return in its body.
         */
        emit_statement(&(struct statement){ .type = STATEMENT_RETURN, .return_value = NULL });

        int bytes = state.code.count - size_offset - sizeof (int);
        LOG("bytes in func = %d", bytes);
        memcpy(state.code.items + size_offset, &bytes, sizeof (int));

        emit_int(e->param_symbols.count);
        for (int i = 0; i < e->param_symbols.count; ++i) {
                emit_symbol(e->param_symbols.items[i]);
        }

        emit_int(state.refs.count);
        for (int i = 0; i < state.refs.count; ++i) {
                emit_symbol(state.refs.items[i].symbol);
                emit_symbol(state.refs.items[i].offset - start_offset);
        }

        state.refs = refs_save;
        state.bound_symbols = syms_save;
}

static void
emit_conditional(struct statement const *s)
{
        emit_expression(s->conditional.cond);
        PLACEHOLDER_JUMP(INSTR_COND_JUMP, then_branch);

        if (s->conditional.else_branch != NULL) {
                emit_statement(s->conditional.else_branch);
        }

        PLACEHOLDER_JUMP(INSTR_JUMP, end);

        PATCH_JUMP(then_branch);

        emit_statement(s->conditional.then_branch);

        PATCH_JUMP(end);
}

static void
emit_and(struct expression const *left, struct expression const *right)
{
        emit_expression(left);
        emit_instr(INSTR_DUP);

        PLACEHOLDER_JUMP(INSTR_NCOND_JUMP, left_false);

        emit_expression(right);
        emit_instr(INSTR_AND);

        PATCH_JUMP(left_false);
}

static void
emit_or(struct expression const *left, struct expression const *right)
{
        emit_expression(left);
        emit_instr(INSTR_DUP);

        PLACEHOLDER_JUMP(INSTR_COND_JUMP, left_true);

        emit_expression(right);
        emit_instr(INSTR_OR);

        PATCH_JUMP(left_true);
}

static void
emit_while_loop(struct statement const *s)
{
        offset_vector cont_save = state.continues;
        offset_vector brk_save = state.breaks;
        vec_init(state.continues);
        vec_init(state.breaks);

        size_t begin = state.code.count;

        emit_expression(s->while_loop.cond);
        PLACEHOLDER_JUMP(INSTR_NCOND_JUMP, end);

        emit_statement(s->while_loop.body);

        JUMP(begin);

        PATCH_JUMP(end);

        patch_loop_jumps(begin, state.code.count);

        state.continues = cont_save;
        state.breaks = brk_save;
}

static void
emit_for_loop(struct statement const *s)
{
        offset_vector cont_save = state.continues;
        offset_vector brk_save = state.breaks;
        vec_init(state.continues);
        vec_init(state.breaks);

        if (s->for_loop.init != NULL) {
                emit_statement(s->for_loop.init);
        }

        PLACEHOLDER_JUMP(INSTR_JUMP, skip_next);

        size_t begin = state.code.count;

        emit_expression(s->for_loop.next);
        emit_instr(INSTR_POP);

        PATCH_JUMP(skip_next);

        emit_expression(s->for_loop.cond);
        PLACEHOLDER_JUMP(INSTR_NCOND_JUMP, end_jump);

        emit_statement(s->for_loop.body);

        JUMP(begin);

        PATCH_JUMP(end_jump);

        patch_loop_jumps(begin, state.code.count);

        state.continues = cont_save;
        state.breaks = brk_save;
}

static void
emit_each_loop(struct statement const *s)
{
        offset_vector cont_save = state.continues;
        offset_vector brk_save = state.breaks;
        vec_init(state.continues);
        vec_init(state.breaks);

        emit_expression(s->each.array);

        int array_sym = tmpsymbol(0);
        emit_instr(INSTR_PUSH_VAR);
        emit_symbol(array_sym);

        int counter_sym = tmpsymbol(1);
        emit_instr(INSTR_PUSH_VAR);
        emit_symbol(counter_sym);

        emit_instr(INSTR_TARGET_VAR);
        emit_symbol(array_sym);
        emit_instr(INSTR_ASSIGN);
        emit_instr(INSTR_POP);

        emit_instr(INSTR_INTEGER);
        emit_integer(-1);

        assert(array_sym != counter_sym);

        emit_instr(INSTR_TARGET_VAR);
        emit_symbol(counter_sym);
        emit_instr(INSTR_ASSIGN);
        emit_instr(INSTR_POP);

        size_t begin = state.code.count;

        emit_instr(INSTR_INC);
        emit_symbol(counter_sym);

        emit_instr(INSTR_LOAD_VAR);
        emit_symbol(counter_sym);
        emit_instr(INSTR_LOAD_VAR);
        emit_symbol(array_sym);
        emit_instr(INSTR_LEN);
        emit_instr(INSTR_LT);

        PLACEHOLDER_JUMP(INSTR_NCOND_JUMP, end);

        struct expression array = { .type = EXPRESSION_VARIABLE, .symbol = array_sym, .local = true };
        struct expression index = { .type = EXPRESSION_VARIABLE, .symbol = counter_sym, .local = true };
        struct expression subscript = { .type = EXPRESSION_SUBSCRIPT, .container = &array, .subscript = &index };
        emit_assignment(s->each.target, &subscript, 2);
        emit_instr(INSTR_POP);
        emit_statement(s->each.body);

        JUMP(begin);

        PATCH_JUMP(end);

        patch_loop_jumps(begin, state.code.count);

        state.continues = cont_save;
        state.breaks = brk_save;
}

static void
emit_target(struct expression *target)
{
        switch (target->type) {
        case EXPRESSION_VARIABLE:
                if (target->local) {
                        emit_instr(INSTR_TARGET_VAR);
                } else {
                        emit_instr(INSTR_TARGET_REF);
                        addref(target->symbol);
                }
                emit_symbol(target->symbol);
                break;
        case EXPRESSION_MEMBER_ACCESS:
                emit_expression(target->object);
                emit_instr(INSTR_TARGET_MEMBER);
                emit_string(target->member_name);
                break;
        case EXPRESSION_SUBSCRIPT:
                emit_expression(target->container);
                emit_expression(target->subscript);
                emit_instr(INSTR_TARGET_SUBSCRIPT);
                break;
        default:
                fail("oh no!");
        }
}

static void
emit_assignment(struct expression *target, struct expression const *e, int i)
{
        int tmp;
        struct expression container, subscript;

        switch (target->type) {
        case EXPRESSION_ARRAY:
                tmp = tmpsymbol(i);
                emit_expression(e);
                emit_instr(INSTR_PUSH_VAR);
                emit_symbol(tmp);
                emit_instr(INSTR_TARGET_VAR);
                emit_symbol(tmp);
                emit_instr(INSTR_ASSIGN);
                container = (struct expression){ .type = EXPRESSION_VARIABLE, .symbol = tmp, .local = true };
                for (int j = 0; j < target->elements.count; ++j) {
                        subscript = (struct expression){ .type = EXPRESSION_INTEGER, .integer = j };
                        emit_assignment(target->elements.items[j], &(struct expression){ .type = EXPRESSION_SUBSCRIPT, .container = &container, .subscript = &subscript }, i + 1);
                        emit_instr(INSTR_POP);
                }
                emit_instr(INSTR_POP_VAR);
                emit_symbol(tmp);
                break;
        default:
                emit_expression(e);
                emit_target(target);
                emit_instr(INSTR_ASSIGN);
        }
}

static void
emit_expression(struct expression const *e)
{
        switch (e->type) {
        case EXPRESSION_VARIABLE:
                if (e->local) {
                        emit_instr(INSTR_LOAD_VAR);
                } else {
                        emit_instr(INSTR_LOAD_REF);
                        addref(e->symbol);
                }
                emit_symbol(e->symbol);
                break;
        case EXPRESSION_ASSIGNMENT:
                emit_assignment(e->target, e->value, 0);
                break;
        case EXPRESSION_INTEGER:
                emit_instr(INSTR_INTEGER);
                emit_integer(e->integer);
                break;
        case EXPRESSION_BOOLEAN:
                emit_instr(INSTR_BOOLEAN);
                emit_boolean(e->boolean);
                break;
        case EXPRESSION_REAL:
                emit_instr(INSTR_REAL);
                emit_float(e->real);
                break;
        case EXPRESSION_STRING:
                emit_instr(INSTR_STRING);
                emit_string(e->string);
                break;
        case EXPRESSION_ARRAY:
                for (int i = e->elements.count - 1; i >= 0; --i) {
                        emit_expression(e->elements.items[i]);
                }
                emit_instr(INSTR_ARRAY);
                emit_int(e->elements.count);
                break;
        case EXPRESSION_OBJECT:
                for (int i = e->keys.count - 1; i >= 0; --i) {
                        emit_expression(e->keys.items[i]);
                        emit_expression(e->values.items[i]);
                }
                emit_instr(INSTR_OBJECT);
                emit_int(e->keys.count);
                break;
        case EXPRESSION_NIL:
                emit_instr(INSTR_NIL);
                break;
        case EXPRESSION_MEMBER_ACCESS:
                emit_expression(e->object);
                emit_instr(INSTR_MEMBER_ACCESS);
                emit_string(e->member_name);
                break;
        case EXPRESSION_SUBSCRIPT:
                emit_expression(e->container);
                emit_expression(e->subscript);
                emit_instr(INSTR_SUBSCRIPT);
                break;
        case EXPRESSION_FUNCTION_CALL:
                for (size_t i = 0; i < e->args.count; ++i) {
                        emit_expression(e->args.items[i]);
                }
                emit_expression(e->function);
                emit_instr(INSTR_CALL);
                emit_int(e->args.count);
                break;
        case EXPRESSION_METHOD_CALL:
                for (size_t i = 0; i < e->method_args.count; ++i) {
                        emit_expression(e->method_args.items[i]);
                }
                emit_expression(e->object);
                emit_instr(INSTR_CALL_METHOD);
                emit_string(e->method_name);
                emit_int(e->method_args.count);
                break;
        case EXPRESSION_FUNCTION:
                emit_instr(INSTR_FUNCTION);
                emit_function(e);
                break;
        case EXPRESSION_PLUS:
                emit_expression(e->left);
                emit_expression(e->right);
                emit_instr(INSTR_ADD);
                break;
        case EXPRESSION_MINUS:
                emit_expression(e->left);
                emit_expression(e->right);
                emit_instr(INSTR_SUB);
                break;
        case EXPRESSION_STAR:
                emit_expression(e->left);
                emit_expression(e->right);
                emit_instr(INSTR_MUL);
                break;
        case EXPRESSION_DIV:
                emit_expression(e->left);
                emit_expression(e->right);
                emit_instr(INSTR_DIV);
                break;
        case EXPRESSION_PERCENT:
                emit_expression(e->left);
                emit_expression(e->right);
                emit_instr(INSTR_MOD);
                break;
        case EXPRESSION_AND:
                emit_and(e->left, e->right);
                break;
        case EXPRESSION_OR:
                emit_or(e->left, e->right);
                break;
        case EXPRESSION_LT:
                emit_expression(e->left);
                emit_expression(e->right);
                emit_instr(INSTR_LT);
                break;
        case EXPRESSION_LEQ:
                emit_expression(e->left);
                emit_expression(e->right);
                emit_instr(INSTR_LEQ);
                break;
        case EXPRESSION_GT:
                emit_expression(e->left);
                emit_expression(e->right);
                emit_instr(INSTR_GT);
                break;
        case EXPRESSION_GEQ:
                emit_expression(e->left);
                emit_expression(e->right);
                emit_instr(INSTR_GEQ);
                break;
        case EXPRESSION_DBL_EQ:
                emit_expression(e->left);
                emit_expression(e->right);
                emit_instr(INSTR_EQ);
                break;
        case EXPRESSION_NOT_EQ:
                emit_expression(e->left);
                emit_expression(e->right);
                emit_instr(INSTR_NEQ);
                break;
        case EXPRESSION_PREFIX_BANG:
                emit_expression(e->operand);
                emit_instr(INSTR_NOT);
                break;
        case EXPRESSION_PREFIX_AT:
                emit_expression(e->operand);
                emit_instr(INSTR_KEYS);
                break;
        case EXPRESSION_PREFIX_MINUS:
                emit_expression(e->operand);
                emit_instr(INSTR_NEG);
                break;
        case EXPRESSION_PREFIX_INC:
                emit_target(e->operand);
                emit_instr(INSTR_PRE_INC);
                break;
        case EXPRESSION_PREFIX_DEC:
                emit_target(e->operand);
                emit_instr(INSTR_PRE_DEC);
                break;
        case EXPRESSION_POSTFIX_INC:
                emit_target(e->operand);
                emit_instr(INSTR_POST_INC);
                break;
        case EXPRESSION_POSTFIX_DEC:
                emit_target(e->operand);
                emit_instr(INSTR_POST_DEC);
                break;
        }
}

static void
emit_statement(struct statement const *s)
{
        switch (s->type) {
        case STATEMENT_BLOCK:
                for (int i = 0; i < s->statements.count; ++i) {
                        emit_statement(s->statements.items[i]);
                }
                break;
        case STATEMENT_CONDITIONAL:
                emit_conditional(s);
                break;
        case STATEMENT_FOR_LOOP:
                emit_for_loop(s);
                break;
        case STATEMENT_EACH_LOOP:
                emit_each_loop(s);
                break;
        case STATEMENT_WHILE_LOOP:
                emit_while_loop(s);
                break;
        case STATEMENT_EXPRESSION:
                emit_expression(s->expression);
                emit_instr(INSTR_POP);
                break;
        case STATEMENT_DEFINITION:
                emit_assignment(s->target, s->value, 0);
                emit_instr(INSTR_POP);
                break;
        case STATEMENT_RETURN:
                if (s->return_value != NULL) {
                        emit_expression(s->return_value);
                } else {
                        emit_instr(INSTR_NIL);
                }
                for (int i = 0; i < state.bound_symbols.count; ++i) {
                        emit_instr(INSTR_POP_VAR);
                        emit_symbol(state.bound_symbols.items[i]);
                }
                emit_instr(INSTR_RETURN);
                break;
        case STATEMENT_BREAK:
                emit_instr(INSTR_JUMP);
                vec_push(state.breaks, state.code.count);
                emit_int(0);
                break;
        case STATEMENT_CONTINUE:
                emit_instr(INSTR_JUMP);
                vec_push(state.continues, state.code.count);
                emit_int(0);
                break;
        }
}

static void
emit_new_globals(void)
{
        for (int i = global_count; i < global->func_symbols.count; ++i) {
                if (global->func_symbols.items[i] >= builtin_count) {
                        emit_instr(INSTR_PUSH_VAR);
                        emit_symbol(global->func_symbols.items[i]);
                }
        }

        global_count = global->func_symbols.count;
}

static struct scope *
get_module_scope(char const *name)
{
        for (int i = 0; i < modules.count; ++i) {
                if (strcmp(name, modules.items[i].path) == 0) {
                        return modules.items[i].scope;
                }
        }

        return NULL;
}

static void
import_module(char *name, char *as)
{
        LOG("IMPORTING %s AS %s", name, as);

        struct scope *module_scope = get_module_scope(name);

        /* First make sure we haven't already imported this module, or imported another module
         * with the same local alias.
         *
         * e.g.,
         *
         * import foo;
         * import foo;
         *
         * and
         *
         * import foo as bar;
         * import baz as bar;
         *
         * are both errors.
         */
        for (int i = 0; i < state.imports.count; ++i) {
                if (strcmp(as, state.imports.items[i].name) == 0) {
                        fail("there is already a module imported under the name '%s'", as);
                }
                if (state.imports.items[i].scope == module_scope) {
                        fail("the module '%s' has already been imported", name);
                }
        }

        /*
         * If we've already generated code to load this module, we can skip to the part of the code
         * where we add the module to the current scope.
         */
        if (module_scope != NULL) {
                goto import;
        }

        char pathbuf[512];
        char const *home = getenv("HOME");
        if (home == NULL) {
                fail("unable to get $HOME from the environment");
        }

        snprintf(pathbuf, sizeof pathbuf, "%s/.plum/%s.plum", home, name);

        char *source = slurp(pathbuf);
        if (source == NULL) {
                fail("failed to read file: %s", pathbuf);
        }

        struct token *ts = lex(source);
        if (ts == NULL) {
                fail("while importing module %s: %s", name, lex_error());
        }

        struct statement **p = parse(ts);
        if (p == NULL) {
                fail("while importing module %s: %s", name, parse_error());
        }

        /*
         * Save the current compiler state so we can restore it after compiling
         * this module.
         */
        struct state save = state;
        state = freshstate();

        for (size_t i = 0; p[i] != NULL; ++i) {
                symbolize_statement(state.global, p[i]);
        }

        emit_new_globals();

        for (size_t i = 0; p[i] != NULL; ++i) {
                emit_statement(p[i]);
        }

        emit_instr(INSTR_HALT);

        module_scope = state.global;

        struct module m = {
                .path = name,
                .code = state.code.items,
                .scope = module_scope
        };

        vec_push(modules, m);

        state = save;

        emit_instr(INSTR_EXEC_CODE);
        emit_symbol((uintptr_t) m.code);

import:

        LOG("ADDING IMPORT WITH NAME: %s", as);
        vec_push(state.imports, ((struct import){ .name = as, .scope = module_scope }));
}

char const *
compiler_error(void)
{
        return err_msg;
}

void
compiler_init(void)
{
        symbol = 0;
        builtin_count = 0;
        global_count = 0;
        global = newscope(NULL, false);

        state = freshstate();
}

void
compiler_introduce_symbol(char const *name)
{
        addsymbol(global, name);
        builtin_count += 1;
}

char *
compiler_compile_source(char const *source, int *symbols)
{
        vec_init(state.code);
        vec_init(state.refs);

        symbol = *symbols;

        struct token *ts = lex(source);
        if (ts == NULL) {
                err_msg = lex_error();
                return NULL;
        }

        struct statement **p = parse(ts);
        if (p == NULL) {
                err_msg = parse_error();
                return NULL;
        }

        if (setjmp(jb) != 0) {
                *symbols = symbol;
                err_msg = err_buf;
                return NULL;
        }

        for (size_t i = 0; p[i] != NULL; ++i) {
                symbolize_statement(state.global, p[i]);
        }

        emit_new_globals();

        for (size_t i = 0; p[i] != NULL; ++i) {
                emit_statement(p[i]);
        }

        emit_instr(INSTR_HALT);

        *symbols = symbol;
        return state.code.items;
}
