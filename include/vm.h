#ifndef VM_H_INCLUDED
#define VM_H_INCLUDED

#include <stdbool.h>
#include <stdarg.h>
#include <stdnoreturn.h>

struct variable;

enum instruction {
        INSTR_LOAD_VAR,
        INSTR_LOAD_REF,
        INSTR_PUSH_VAR,
        INSTR_POP_VAR,
        INSTR_TARGET_VAR,
        INSTR_TARGET_REF,
        INSTR_TARGET_MEMBER,
        INSTR_TARGET_SUBSCRIPT,
        INSTR_ASSIGN,

        INSTR_ARRAY_REST,

        INSTR_INTEGER,
        INSTR_REAL,
        INSTR_BOOLEAN,
        INSTR_STRING,
        INSTR_REGEX,
        INSTR_ARRAY,
        INSTR_OBJECT,
        INSTR_NIL,
        INSTR_TAG,

        INSTR_TO_STRING,
        INSTR_CONCAT_STRINGS,

        INSTR_RANGE,

        INSTR_MEMBER_ACCESS,
        INSTR_SUBSCRIPT,
        INSTR_CALL,
        INSTR_CALL_METHOD,
        INSTR_POP,
        INSTR_DUP,
        INSTR_LEN,

        INSTR_PRE_INC,
        INSTR_POST_INC,
        INSTR_PRE_DEC,
        INSTR_POST_DEC,

        INSTR_INC,

        INSTR_FUNCTION,
        INSTR_JUMP,
        INSTR_JUMP_IF,
        INSTR_JUMP_IF_NOT,
        INSTR_RETURN,
        INSTR_EXEC_CODE,
        INSTR_HALT,

        INSTR_SAVE_STACK_POS,
        INSTR_RESTORE_STACK_POS,

        INSTR_TAG_PUSH,

        INSTR_TRY_INDEX,
        INSTR_TRY_TAG_POP,
        INSTR_TRY_REGEX,
        INSTR_TRY_ASSIGN_NON_NIL,
        INSTR_BAD_MATCH,
        INSTR_UNTAG_OR_DIE,
        INSTR_ENSURE_LEN,

        // binary operators
        INSTR_ADD,
        INSTR_SUB,
        INSTR_MUL,
        INSTR_DIV,
        INSTR_MOD,
        INSTR_EQ, 
        INSTR_NEQ,
        INSTR_LT, 
        INSTR_GT, 
        INSTR_LEQ,
        INSTR_GEQ,

        INSTR_MUT_ADD,
        INSTR_MUT_MUL,
        INSTR_MUT_DIV,
        INSTR_MUT_SUB,

        // unary operators
        INSTR_NEG,
        INSTR_NOT,
        INSTR_KEYS,
};

void
vm_init(void);

char const *
vm_error(void);

noreturn void
vm_panic(char const *fmt, ...);

void
vm_append_output(char const *s, int n);

char *
vm_get_output(void);

void
vm_mark(void);

void
vm_mark_variable(struct variable *);

void
vm_sweep_variables(void);

bool
vm_execute(char const *source);

bool
vm_execute_file(char const *path);

struct value
vm_eval_function(struct value const * restrict f, struct value * restrict v);

struct value
vm_eval_function2(struct value *f, struct value *v1, struct value *v2);

#endif
