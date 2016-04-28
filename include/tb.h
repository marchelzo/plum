#ifndef TB_H_INCLUDED
#define TB_H_INCLUDED

#include <stddef.h>
#include <stdbool.h>

#include <pcre.h>

#include "vec.h"

struct edit;

struct tb {
        char *left;
        char *right;

        int leftcount;
        int rightcount;

        int capacity;

        int line;
        int column;
        int character;

        int lines;
        int characters;

        /* The column that we should try to be in */
        int highcol;

        vec(int *) markers;
        int markers_allocated;

        vec(struct edit) edits;
        bool record_history; // will only keep track of changes if true
        int history_usage;   // # of bytes being used to store history
        int history_index;

        bool changed;
};

char *
tb_cstr(struct tb const *s);

int
tb_size(struct tb const *s);

int
tb_remove(struct tb *s, int n);

void
tb_clear(struct tb *s);

void
tb_clear_left(struct tb *s);

void
tb_clear_right(struct tb *s);

void
tb_murder(struct tb *s);

void
tb_insert(struct tb *s, char const *data, int n);

int
tb_seek(struct tb *s, int i);

struct tb
tb_new(void);

void
tb_draw(struct tb const *s, char *out, int line, int col, int lines, int cols);

int
tb_read(struct tb *s, int fd);

int
tb_down(struct tb *s, int n);

int
tb_up(struct tb *s, int n);

int
tb_forward(struct tb *s, int n);

int
tb_backward(struct tb *s, int n);

int
tb_right(struct tb *s, int n);

int
tb_left(struct tb *s, int n);

char *
tb_clone_line(struct tb const *s);

void
tb_start_of_line(struct tb *s);

void
tb_end_of_line(struct tb *s);

void
tb_start(struct tb *s);

void
tb_end(struct tb *s);

struct value
tb_get_char(struct tb const *s, int i);

struct value
tb_get_line(struct tb const *s, int i);

int
tb_truncate_line(struct tb *s);

int *
tb_new_marker(struct tb *s, int pos);

void
tb_delete_marker(struct tb *s, int *marker);

void
tb_append_line(struct tb *s, char const *data, int n);

void
tb_append(struct tb *s, char const *data, int n);

void
tb_start_new_edit(struct tb *s);

bool
tb_undo(struct tb *s);

bool
tb_redo(struct tb *s);

int
tb_line_width(struct tb *s);

bool
tb_next_match_regex(struct tb *s, pcre *re, pcre_extra *extra);

bool
tb_next_match_string(struct tb *s, char const *p, int bytes);

inline static int
tb_lines(struct tb const *s)
{
        return (s->characters == 0) ? 0 : (s->lines + 1);
}

inline static int
tb_line(struct tb const *s)
{
        return s->line;
}

inline static int
tb_column(struct tb const *s)
{
        return s->column;
}

inline static void
tb_start_history(struct tb *s)
{
        s->record_history = true;
}

inline static void
tb_stop_history(struct tb *s)
{
        s->record_history = false;
}

int
tb_write(struct tb const *s, int fd);

bool
tb_find_next(struct tb *s, char const *c, int n);

bool
tb_find_prev(struct tb *s, char const *c, int n);

void
tb_each_line(struct tb const *s, struct value *f);

#endif
