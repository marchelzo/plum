#ifndef TERM_H_INCLUDED
#define TERM_H_INCLUDED

#include "editor.h"

void
term_init(struct editor *);

void
term_handle_input(void);

void
term_suspend(void);

void
term_resume(void);

int
term_height(void);

int
term_width(void);

#endif
