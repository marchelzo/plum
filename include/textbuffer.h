#ifndef TEXTBUFFER_H_INCLUDED
#define TEXTBUFFER_H_INCLUDED

#include <stddef.h>

#include "location.h"
#include "vec.h"

struct edit;

struct textbuffer {

        int bytes;
        int lines;


        int line;

        struct line *first;
        struct line *current;
        struct line *last;

        /*
         * Only linear undo. For now, anyway.
         */
        vec(struct edit) history;

        /*
         * All of the markers that we need to keep updated.
         */
        vec(struct location *) markers;
        int markers_allocated;

        /* The right-most column that we should move into upon changing lines. */
        int high_col;
};

struct textbuffer
textbuffer_new(void);

void
textbuffer_insert(struct textbuffer *b, struct location *loc, char const *data);

void
textbuffer_insert_n(struct textbuffer *b, struct location *loc, char const *data, int n);

void
textbuffer_append_n(struct textbuffer *b, struct location *loc, char const *data, int n);

int
textbuffer_move_forward(struct textbuffer *b, struct location *loc, int n);

int
textbuffer_move_backward(struct textbuffer *b, struct location *loc, int n);

int
textbuffer_move_right(struct textbuffer *b, struct location *loc, int n);

int
textbuffer_move_left(struct textbuffer *b, struct location *loc, int n);

int
textbuffer_next_line(struct textbuffer *b, struct location *loc, int n);

int
textbuffer_prev_line(struct textbuffer *b, struct location *loc, int n);

int
textbuffer_remove(struct textbuffer *b, struct location loc, int n);

void
textbuffer_remove_line(struct textbuffer *b, int i);

char *
textbuffer_get_line(struct textbuffer *b, int line);

int
textbuffer_num_lines(struct textbuffer *b);

int
textbuffer_line_width(struct textbuffer *b, int line);

void
textbuffer_cut_line(struct textbuffer *b, struct location loc);

void
textbuffer_print(struct textbuffer *b);

char *
textbuffer_copy_line_cols(struct textbuffer *b, int line, char *out, int start, int n);

struct location const *
textbuffer_new_marker(struct textbuffer *b, struct location loc);

#endif
