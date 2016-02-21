#ifndef TEXTBUFFER_H_INCLUDED
#define TEXTBUFFER_H_INCLUDED

#include <stddef.h>

#include "location.h"

struct textbuffer {
        size_t size;
        size_t count;
        size_t line;
        struct line *first;
        struct line *current;
        struct line *last;
};

struct textbuffer
textbuffer_new(void);

struct location
textbuffer_insert(struct textbuffer *b, struct location loc, char const *data);

void
textbuffer_remove(struct textbuffer *b, struct location loc, size_t n);

void
textbuffer_remove_line(struct textbuffer *b, size_t i);

char const *
textbuffer_get_line_as_cstr(struct textbuffer *b, size_t i, char **out, size_t *n, bool *modified);

size_t
textbuffer_num_lines(struct textbuffer *b);

size_t
textbuffer_line_width(struct textbuffer *b, size_t line);

void
textbuffer_print(struct textbuffer *b);

char *
textbuffer_copy_line_cols(struct textbuffer *b, size_t line, char *out, size_t start, size_t n);

#endif
