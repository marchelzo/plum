#ifndef RENDER_H_INCLUDED
#define RENDER_H_INCLUDED

#include <stdbool.h>

#include <tickit.h>

#include "editor.h"

bool
render(struct editor *e, TickitTerm *term);

#endif
