#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <assert.h>

#include <tickit.h>

#include "panic.h"
#include "alloc.h"
#include "util.h"
#include "str.h"
#include "test.h"
#include "log.h"

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
stringcount(char const *s, int byte_lim, int col_lim, int grapheme_lim)
{
        limitpos.bytes = byte_lim;
        limitpos.columns = col_lim;
        limitpos.graphemes = grapheme_lim;

        tickit_string_ncount(s, byte_lim, &outpos, &limitpos);
}

inline static void
print(struct str *s)
{
        int i;
        for (i = 0; i < s->leftcount; ++i) {
                putchar(s->left[i]);
        }
        for (i = 0; i < s->rightcount; ++i) {
                putchar(s->right[s->rightcount - i - 1]);
        }
        putchar('\n');
}

inline static void
grow(struct str *s, int n)
{
        if (s->capacity > n + 1) {
                return;
        }

        int oldcapacity = s->capacity;

        while (s->capacity < n + 1) {
                s->capacity *= 2;
        }

        resize(s->left, s->capacity);
        resize(s->right, s->capacity);

        memmove(s->right + s->capacity - s->rightcount, s->right + oldcapacity - s->rightcount, s->rightcount);
}

inline static void
seekend(struct str *s)
{
        stringcount(s->right + s->capacity - s->rightcount, s->rightcount, -1, -1);

        memcpy(s->left + s->leftcount, s->right + s->capacity - s->rightcount, s->rightcount);

        s->leftcount += s->rightcount;
        s->rightcount = 0;

        s->col += outpos.columns;
        s->grapheme += outpos.graphemes;
}

/*
 * Seek to the ith column within the string.
 */
inline static void
seekcol(struct str *s, int i)
{
        if (s->col == i) {
                return;
        }

        if (s->col < i) {
                stringcount(s->right + s->capacity - s->rightcount, s->rightcount, i - s->col, -1);
                memcpy(s->left + s->leftcount, s->right + s->capacity - s->rightcount, outpos.bytes);

                s->leftcount += outpos.bytes;
                s->rightcount -= outpos.bytes;

                s->grapheme += outpos.graphemes;

        } else {
                stringcount(s->left, s->leftcount, i, -1);
                memcpy(s->right + s->capacity - s->rightcount - s->leftcount + outpos.bytes, s->left + outpos.bytes, s->leftcount - outpos.bytes);

                s->rightcount += s->leftcount - outpos.bytes;
                s->leftcount = outpos.bytes;

                s->grapheme = outpos.graphemes;
        }

        s->col = i;
}

/*
 * Seek to the ith grapheme within the string.
 */
inline static void
seekgphm(struct str *s, int i)
{
        if (s->grapheme == i) {
                return;
        }

        if (s->grapheme < i) {
                stringcount(s->right + s->capacity - s->rightcount, s->rightcount, -1, i - s->grapheme);
                memcpy(s->left + s->leftcount, s->right + s->capacity - s->rightcount, outpos.bytes);

                s->leftcount += outpos.bytes;
                s->rightcount -= outpos.bytes;

                s->col += outpos.columns;
        } else {
                stringcount(s->left, s->leftcount, -1, i);
                memcpy(s->right + s->capacity - s->rightcount - s->leftcount + outpos.bytes, s->left + outpos.bytes, s->leftcount - outpos.bytes);

                s->rightcount += s->leftcount - outpos.bytes;
                s->leftcount = outpos.bytes;

                s->col = outpos.columns;
        }

        s->grapheme = i;
}

inline static void
push(struct str *s, char const *data)
{
        stringcount(data, -1, -1, -1);

        grow(s, s->leftcount + outpos.bytes);
        memcpy(s->left + s->leftcount, data, outpos.bytes);

        s->leftcount += outpos.bytes;

        s->col += outpos.columns;
        s->cols += outpos.columns;

        s->grapheme += outpos.graphemes;
        s->graphemes += outpos.graphemes;
}

inline static void
pushn(struct str *s, char const *data, int n)
{
        stringcount(data, n, -1, -1);

        grow(s, s->leftcount + n);
        memcpy(s->left + s->leftcount, data, n);

        s->leftcount += outpos.bytes;

        s->col += outpos.columns;
        s->cols += outpos.columns;

        s->grapheme += outpos.graphemes;
        s->graphemes += outpos.graphemes;
}

inline static void
copyto(struct str *s, char *out)
{
        int i = 0;

        for (int j = 0; j < s->leftcount; ++j) {
                out[i++] = s->left[j];
        }
        for (int j = 1; j <= s->rightcount; ++j) {
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
                .cols       = 0,
                .grapheme   = 0,
                .graphemes  = 0,
        };
}

int
str_insert(struct str *s, int *i, char const *data)
{
        seekcol(s, *i);

        int start_chars = s->graphemes;

        push(s, data);

        *i = s->col;
        return s->graphemes - start_chars;
}

/*
 * Insert n bytes.
 */
int
str_insert_n(struct str *s, int *i, int n, char const *data)
{
        seekcol(s, *i);

        int start_chars = s->graphemes;

        pushn(s, data, n);
        
        *i = s->col;
        return s->graphemes - start_chars;
}

/*
 * Remove n graphemes.
 *
 * Returns the number of columns removed.
 */
int
str_remove(struct str *s, int i, int n)
{
        seekcol(s, i);

        stringcount(s->right + s->capacity - s->rightcount, s->rightcount, -1, n);

        s->rightcount -= outpos.bytes;
        s->cols -= outpos.columns;
        s->graphemes -= outpos.graphemes;

        return outpos.columns;
}

void
str_remove_chars(struct str *s, int col, int n, int *bytes, int *chars)
{
        seekcol(s, col);

        stringcount(s->right + s->capacity - s->rightcount, s->rightcount, -1, n);

        s->rightcount -= outpos.bytes;
        s->cols -= outpos.columns;
        s->graphemes -= outpos.graphemes;

        *chars = outpos.graphemes;
        *bytes = outpos.bytes;
}

void
str_truncate(struct str *s, int i)
{
        seekcol(s, i);
        s->rightcount = 0;
        s->cols = s->col;
        s->graphemes = s->grapheme;
}

int
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
        
        s->grapheme = 0;
        s->graphemes = 0;
}

bool
str_is_empty(struct str const *s)
{
        return (s->leftcount + s->rightcount) == 0;
}

int
str_compare_cstr(struct str const *s, char const *cstr)
{
        int n = strlen(cstr);

        int k = strncmp(s->left, cstr, s->leftcount);

        if (k != 0) {
                return k;
        }
        if (n < s->leftcount) {
                return 1;
        }

        return strncmp(s->right + s->capacity - s->rightcount, cstr + s->leftcount, s->rightcount);
}

char *
str_cstr(struct str const *s)
{
        char *cstr = malloc(s->leftcount + s->rightcount + 1);
        memcpy(cstr, s->left, s->leftcount);
        memcpy(cstr + s->leftcount, s->right + s->capacity - s->rightcount, s->rightcount);
        cstr[s->leftcount + s->rightcount] = '\0';
        return cstr;
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

        assert(s != NULL);
        assert(other != NULL);

        grow(s, s->leftcount + s->rightcount + other->leftcount + other->rightcount);

        pushn(s, other->left, other->leftcount);
        pushn(s, other->right + other->capacity - other->rightcount, other->rightcount);
}

struct str
str_split(struct str *s, int i)
{

        seekcol(s, i);

        stringcount(s->right + s->capacity - s->rightcount, s->rightcount, -1, -1);

        struct str new = {
                .leftcount = 0,
                .rightcount = s->rightcount,
                .capacity = s->capacity,
                .left = alloc(s->capacity + 1),
                .right = s->right,
                .col = 0,
                .cols = s->cols - s->col,
                .grapheme = 0,
                .graphemes = s->graphemes - s->grapheme
        };

        s->right = alloc(s->capacity + 1);
        s->rightcount = 0;

        s->cols = s->col;
        s->graphemes = s->grapheme;

        return new;
}

char *
str_copy_cols(struct str const *s, char *out, int start, int n)
{
        char *data;

        assert(s != NULL);
        if (start > s->col) {
                start -= s->col;
                data = s->right + s->capacity - s->rightcount;

                stringcount(data, s->rightcount, start, -1);

                data += outpos.bytes;

                stringcount(data, s->rightcount - outpos.bytes, n, -1);

                int bytes = outpos.bytes;
                memcpy(out, &bytes, sizeof (int));
                memcpy(out + sizeof (int), data, outpos.bytes);

                return out + sizeof (int) + bytes;
        } else if (s->col - start >= n) {
                data = s->left;

                stringcount(data, s->leftcount, start, -1);
                int leftskip = outpos.bytes;

                stringcount(s->left + leftskip, s->leftcount - leftskip, n, -1);

                int bytes = outpos.bytes;

                memcpy(out, &bytes, sizeof (int));
                memcpy(out + sizeof (int), s->left + leftskip, bytes);

                return out + sizeof (int) + bytes;
        } else {
                data = s->left;

                stringcount(data, s->leftcount, start, -1);

                int leftskip = outpos.bytes;

                stringcount(s->right + s->capacity - s->rightcount, s->rightcount, n - (s->col - start), -1);

                int leftbytes = s->leftcount - leftskip;
                int bytes = leftbytes + outpos.bytes;

                memcpy(out, &bytes, sizeof (int));
                memcpy(out + sizeof (int), s->left + leftskip, leftbytes);
                memcpy(out + sizeof (int) + leftbytes, s->right + s->capacity - s->rightcount, outpos.bytes);

                return out + sizeof (int) + bytes;
        }
}

int
str_width(struct str const *s)
{
        return s->cols;
}

int
str_graphemes(struct str const *s)
{
        return s->graphemes;
}

/*
 * Move left 'n' graphemes from column 'i'.
 *
 * Returns the number of graphemes moved.
 */
int
str_move_left(struct str *s, int *i, int n)
{
        seekcol(s, *i);
        int gphm = s->grapheme;
        seekgphm(s, s->grapheme - min(n, s->grapheme));
        *i = s->col;

        return gphm - s->grapheme;
}

/*
 * Move right 'n' graphemes from column 'i'.
 *
 * Returns the number of graphemes moved.
 */
int
str_move_right(struct str *s, int *i, int n)
{
        LOG("col = %d", s->col);
        seekcol(s, *i);
        int gphm = s->grapheme;
        LOG("col = %d", s->col);
        LOG("gphm = %d", s->grapheme);
        seekgphm(s, min(s->grapheme + n, s->graphemes));
        *i = s->col;
        LOG("gphm = %d", s->grapheme);
        LOG("col = %d", s->col);
        return s->grapheme - gphm;
}

/*
 * Tries to move to the ith column in the string, but may
 * only go to column (i - 1) if necessary to avoid being on
 * the right side of a 2-column grapheme.
 */
int
str_move_to(struct str *s, int i)
{
        seekend(s);
        stringcount(s->left, s->leftcount, i, -1);
        return outpos.columns;
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

        int original_width = str_width(&s);

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

        str_insert(&s, &(int){3}, "def");

        str_copy_cols(&s, buf, 3, 9);
        claim(strcmp(buf + sizeof (int), "deffoobar") == 0);
}

TEST(width)
{
        struct str s = str_new();

        str_push(&s, "ႠႡႢႣༀ༁⌛☠♊");
        claim(str_width(&s) == 9);
}

TEST(cstr)
{
        struct str s = str_new();
        str_push(&s, "abc");
        str_push(&s, " ");
        str_push(&s, "foo");

        claim(strcmp(str_cstr(&s), "abc foo") == 0);
}

TEST(cjk_remove)
{
        struct str s = str_new();
        str_push(&s, "乔 乕 乖 乗 乘");
        claim(str_width(&s) == 14);

        str_remove(&s, 0, 2);
        claim(strcmp(str_cstr(&s), "乕 乖 乗 乘") == 0);
        claim(str_width(&s) == 11);
}

TEST(move_to)
{
        struct str s = str_new();
        str_push(&s, "乔 乕 乖 乗 乘");
        claim(str_width(&s) == 14);

        claim(str_move_to(&s, 1) == 0);
        claim(str_move_to(&s, 2) == 2);
        claim(str_move_to(&s, 3) == 3);
        claim(str_move_to(&s, 4) == 3);
        claim(str_move_to(&s, 8) == 8);
        claim(str_move_to(&s, 10) == 9);
}
