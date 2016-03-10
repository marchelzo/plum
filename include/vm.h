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
        INSTR_INTEGER,
        INSTR_REAL,
        INSTR_BOOLEAN,
        INSTR_STRING,
        INSTR_ARRAY,
        INSTR_OBJECT,
        INSTR_NIL,
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
        INSTR_COND_JUMP,
        INSTR_NCOND_JUMP,
        INSTR_RETURN,
        INSTR_EXEC_CODE,
        INSTR_HALT,

        // binary operators
        INSTR_ADD,
        INSTR_SUB,
        INSTR_MUL,
        INSTR_DIV,
        INSTR_MOD,
        INSTR_AND,
        INSTR_OR, 
        INSTR_EQ, 
        INSTR_NEQ,
        INSTR_LT, 
        INSTR_GT, 
        INSTR_LEQ,
        INSTR_GEQ,

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
vm_mark(void);

void
vm_mark_variable(struct variable *);

void
vm_sweep_variables(void);

bool
vm_execute(char const *source);

#endif
