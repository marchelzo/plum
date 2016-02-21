#ifndef STR_H_INCLUDED
#define STR_H_INCLUDED

#include <stddef.h>
#include <stdbool.h>

struct str {
        size_t leftcount;
        size_t rightcount;

        size_t capacity;

        size_t col;
        size_t cols;

        char *left;
        char *right;
};

struct str
str_new(void);

void
str_insert(struct str *s, size_t i, char const *cstr);

void
str_insert_n(struct str *s, size_t i, size_t n, char const *cstr);

void
str_remove(struct str *s, size_t i, size_t n);

size_t
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
str_split(struct str *s, size_t i);

void
str_truncate(struct str *s, size_t i);

char *
str_copy_cols(struct str const *s, char *out, size_t start, size_t n);

size_t
str_width(struct str const *s);

#endif
