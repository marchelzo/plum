#ifndef STR_H_INCLUDED
#define STR_H_INCLUDED

#include <stddef.h>
#include <stdbool.h>

struct str {
        int leftcount;
        int rightcount;

        int capacity;

        int col;
        int cols;

        int grapheme;
        int graphemes;

        char *left;
        char *right;
};

struct str
str_new(void);

int
str_insert(struct str *s, int *i, char const *data);

int
str_insert_n(struct str *s, int *i, int n, char const *data);

int
str_remove(struct str *s, int i, int n);

void
str_remove_chars(struct str *s, int col, int n, int *bytes, int *chars);

int
str_size(struct str const *s);

int
str_compare_cstr(struct str const *s, char const *cstr);

void
str_push(struct str *s, char const *cstr);

void
str_append(struct str *s, struct str *other);

void
str_free(struct str *s);

void
str_reset(struct str *s);

bool
str_is_empty(struct str const *s);

struct str
str_split(struct str *s, int i);

void
str_truncate(struct str *s, int i);

char *
str_cstr(struct str const *s);

char *
str_copy_cols(struct str const *s, char *out, int start, int n);

int
str_width(struct str const *s);

int
str_move_left(struct str *s, int *i, int n);

int
str_move_right(struct str *s, int *i, int n);

int
str_move_to(struct str *s, int i);

#endif
