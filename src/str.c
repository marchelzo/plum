#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <assert.h>

#include <tickit.h>

#include "panic.h"
#include "alloc.h"
#include "str.h"
#include "test.h"

enum {
        INITIAL_CAPACITY    = 64,
        CSTR_SEEK_THRESHOLD = 16
};

static TickitStringPos limitpos = {
        .codepoints = -1,
        .graphemes = -1
};


static TickitStringPos outpos;

inline static void
stringcount(char const *s, int byte_lim, int col_lim)
{
        limitpos.bytes = byte_lim;
        limitpos.columns = col_lim;

        tickit_string_ncount(s, byte_lim, &outpos, &limitpos);
}

inline static void
print(struct str *s)
{
        size_t i;
        for (i = 0; i < s->leftcount; ++i) {
                putchar(s->left[i]);
        }
        for (i = 0; i < s->rightcount; ++i) {
                putchar(s->right[s->rightcount - i - 1]);
        }
        putchar('\n');
}

inline static void
grow(struct str *s, size_t n)
{
        if (s->capacity > n + 1) {
                return;
        }

        size_t oldcapacity = s->capacity;

        while (s->capacity < n + 1) {
                s->capacity *= 2;
        }

        resize(s->left, s->capacity);

        char *newright = alloc(s->capacity);
        memcpy(newright + s->capacity - s->rightcount, s->right + oldcapacity - s->rightcount, s->rightcount);
        free(s->right);
        s->right = newright;
}

inline static void
seekend(struct str *s)
{
        stringcount(s->right + s->capacity - s->rightcount, s->rightcount, -1);

        memcpy(s->left + s->leftcount, s->right + s->capacity - s->rightcount, s->rightcount);

        s->leftcount += s->rightcount;
        s->rightcount = 0;
        s->col += outpos.columns;
}

/*
 * Seek to the ith column within the string.
 */
inline static void
seekto(struct str *s, size_t i)
{
        if (s->col == i) {
                return;
        }

        if (s->col < i) {
                stringcount(s->right + s->capacity - s->rightcount, -1, i - s->col);
                memcpy(s->left + s->leftcount, s->right + s->capacity - s->rightcount, outpos.bytes);
                s->leftcount += outpos.bytes;
                s->rightcount -= outpos.bytes;
        } else {
                stringcount(s->left, -1, i);
                memcpy(s->right + s->capacity - s->rightcount - s->leftcount + outpos.bytes, s->left + outpos.bytes, s->leftcount - outpos.bytes);
                s->rightcount += s->leftcount - outpos.bytes;
                s->leftcount = outpos.bytes;
        }

        s->col = i;
}

inline static void
push(struct str *s, char const *data)
{
        stringcount(data, -1, -1);

        grow(s, s->leftcount + outpos.bytes);
        memcpy(s->left + s->leftcount, data, outpos.bytes);

        s->leftcount += outpos.bytes;
        s->col += outpos.columns;
        s->cols += outpos.columns;
}

inline static void
pushn(struct str *s, char const *data, size_t n)
{
        stringcount(data, n, -1);

        grow(s, s->leftcount + n);
        memcpy(s->left + s->leftcount, data, n);

        s->leftcount += n;
        s->col += outpos.columns;
        s->cols += outpos.columns;
}

inline static void
copyto(struct str *s, char *out)
{
        size_t i = 0;

        for (size_t j = 0; j < s->leftcount; ++j) {
                out[i++] = s->left[j];
        }
        for (size_t j = 1; j <= s->rightcount; ++j) {
                out[i++] = s->right[s->rightcount - j];
        }

        out[i] = '\0';
}

struct str
str_new(void)
{
        return (struct str) {
                .left       = alloc(INITIAL_CAPACITY + 1),
                .right      = alloc(INITIAL_CAPACITY + 1),
                .leftcount  = 0,
                .rightcount = 0,
                .capacity   = INITIAL_CAPACITY,
                .col        = 0,
                .cols       = 0
        };
}

void
str_insert(struct str *s, size_t i, char const *data)
{
        seekto(s, i);
        push(s, data);
}

void
str_insert_n(struct str *s, size_t i, size_t n, char const *data)
{
        seekto(s, i);
        pushn(s, data, n);
}

void
str_remove(struct str *s, size_t i, size_t n)
{
        seekto(s, i);
        stringcount(s->right + s->capacity - s->rightcount, s->rightcount, n);
        s->rightcount -= outpos.bytes;
        s->cols -= outpos.columns;
}

void
str_truncate(struct str *s, size_t i)
{
        seekto(s, i);
        s->rightcount = 0;
        s->cols = s->col;
}

size_t
str_size(struct str const *s)
{
        return (s->leftcount + s->rightcount);
}

void
str_free(struct str *s)
{
        free(s->left);
        free(s->right);
}

void
str_reset(struct str *s)
{
        str_free(s);
        s->left = s->right = NULL;
        s->leftcount = s->rightcount = 0;
        s->capacity = 0;
        s->col = 0;
        s->cols = 0;
}

bool
str_is_empty(struct str const *s)
{
        return (s->leftcount + s->rightcount) == 0;
}

int
str_compare_cstr(struct str const *s, char const *cstr)
{
        size_t n = strlen(cstr);

        int k = strncmp(s->left, cstr, s->leftcount);

        if (k != 0) {
                return k;
        }
        if (n < s->leftcount) {
                return 1;
        }

        return strncmp(s->right + s->capacity - s->rightcount, cstr + s->leftcount, s->rightcount);
}

void
str_push(struct str *s, char const *data)
{
        seekend(s);
        push(s, data);
}

void
str_append(struct str *s, struct str *other)
{
        seekend(s);

        grow(s, s->leftcount + s->rightcount + other->leftcount + other->rightcount);

        pushn(s, other->left, other->leftcount);
        pushn(s, other->right + other->capacity - other->rightcount, other->rightcount);
}

struct str
str_split(struct str *s, size_t i)
{
    seekto(s, i);
    
    stringcount(s->right + s->capacity - s->rightcount, s->rightcount, -1);

    struct str new = {
        .leftcount = 0,
        .rightcount = s->rightcount,
        .capacity = s->capacity,
        .left = alloc(s->capacity + 1),
        .right = s->right,
        .col = outpos.columns
    };

    s->cols = s->col;
    s->right = alloc(s->capacity + 1);
    s->rightcount = 0;
    
    return new;
}

char *
str_copy_cols(struct str const *s, char *out, size_t start, size_t n)
{
        char *data;

        assert(s != NULL);
        if (start > s->col) {
                start -= s->col;
                data = s->right + s->capacity - s->rightcount;

                stringcount(data, s->rightcount, start);

                data += outpos.bytes;

                stringcount(data, s->rightcount - outpos.bytes, n);

                int bytes = outpos.bytes;
                memcpy(out, &bytes, sizeof (int));
                memcpy(out + sizeof (int), data, outpos.bytes);

                return out + sizeof (int) + bytes;
        } else if (s->col - start >= n) {
                data = s->left;

                stringcount(data, s->leftcount, start);
                size_t leftskip = outpos.bytes;

                stringcount(s->left + leftskip, s->leftcount - leftskip, n);

                int bytes = outpos.bytes;

                memcpy(out, &bytes, sizeof (int));
                memcpy(out + sizeof (int), s->left + leftskip, bytes);

                return out + sizeof (int) + bytes;
        } else {
                data = s->left;

                stringcount(data, s->leftcount, start);

                size_t leftskip = outpos.bytes;

                stringcount(s->right + s->capacity - s->rightcount, s->rightcount, n - (s->col - start));

                int leftbytes = s->leftcount - leftskip;
                int bytes = leftbytes + outpos.bytes;

                memcpy(out, &bytes, sizeof (int));
                memcpy(out + sizeof (int), s->left + leftskip, leftbytes);
                memcpy(out + sizeof (int) + leftbytes, s->right + s->capacity - s->rightcount, outpos.bytes);

                return out + sizeof (int) + bytes;
        }
}

size_t
str_width(struct str const *s)
{
        return s->cols;
}

TEST(create)
{
        struct str s = str_new();

        claim(s.leftcount == 0);
        claim(s.rightcount == 0);

        claim(s.left != NULL);
        claim(s.right != NULL);

        claim(s.capacity == INITIAL_CAPACITY);
}

TEST(push)
{
        struct str s = str_new();
        str_push(&s, "hello");

        claim(s.capacity == INITIAL_CAPACITY);
        claim(s.leftcount == 5);
        claim(s.rightcount == 0);

        claim(s.left != NULL);
        claim(s.right != NULL);
}

TEST(size)
{
        struct str s = str_new();

        str_push(&s, "hello");
        str_push(&s, " ");
        str_push(&s, "world");

        claim(str_size(&s) == 11);
}

TEST(remove)
{
        struct str s = str_new();

        str_push(&s, "hello");
        str_push(&s, " ");
        str_push(&s, "world");

        size_t original_width = str_width(&s);

        str_remove(&s, 5, 1);
        claim(str_compare_cstr(&s, "helloworld") == 0);

        claim(str_width(&s) == original_width - 1);

        str_remove(&s, 0, 11);

        claim(str_size(&s) == 0);
}

TEST(truncate)
{
        struct str s = str_new();

        str_push(&s, "hello");
        str_push(&s, " ");
        str_push(&s, "world");

        str_truncate(&s, 5);

        claim(str_compare_cstr(&s, "hello") == 0);
}

TEST(cols)
{
        struct str s = str_new();

        str_push(&s, "abc");
        str_push(&s, "foobar");
        str_push(&s, "123");

        char buf[32] = {0};
        str_copy_cols(&s, buf, 3, 6);

        claim(strcmp(buf + sizeof (int), "foobar") == 0);

        str_insert(&s, 3, "def");

        str_copy_cols(&s, buf, 3, 9);
        claim(strcmp(buf + sizeof (int), "deffoobar") == 0);
}

TEST(width)
{
        struct str s = str_new();

        str_push(&s, "ႠႡႢႣༀ༁⌛☠♊");
        claim(str_width(&s) == 9);
}
