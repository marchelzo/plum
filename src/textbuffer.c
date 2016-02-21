#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "str.h"
#include "panic.h"
#include "alloc.h"
#include "test.h"
#include "location.h"
#include "textbuffer.h"

struct line {
        struct line *prev;
        struct str data;
        struct line *next;
};

struct edit {
        enum { EDIT_INSERT, EDIT_DELETE } type;
        struct location loc;
        char *data;
};

struct editgroup {
        struct edit edits;
};

static struct line *
line_new(struct line *prev, struct str data, struct line *next)
{
        struct line *line = alloc(sizeof *line);
        line->prev = prev;
        line->data = data;
        line->next = next;
        return line;
}

inline static void
nextline(struct textbuffer *b)
{
        b->current = b->current->next;
        b->line += 1;
}

inline static void
prevline(struct textbuffer *b)
{
        b->current = b->current->prev;
        b->line -= 1;
}

static void
seekline(struct textbuffer *b, size_t line)
{
        while (b->line < line) {
                nextline(b);
        }
        while (b->line > line) {
                prevline(b);
        }
}

inline static void
newline(struct textbuffer *b)
{
        struct line *new = line_new(b->current, str_new(), b->current->next);
        if (b->current->next != NULL) {
                b->current->next->prev = new;
        } else {
                b->last = new;
        }
        b->current->next = new;
        b->count += 1;
        nextline(b);
}

inline static void
joinline(struct textbuffer *b)
{
        struct line *next = b->current->next;

        str_append(&b->current->data, &next->data);
        b->current->next = next->next;
        if (next->next != NULL) {
                next->next->prev = b->current;
        }

        str_free(&next->data);
        free(next);

        b->size -= 1;
}

inline static void
removeline(struct textbuffer *b)
{
        struct line *current = b->current;

        if (current->next != NULL) {
                current->next->prev = current->prev;
                b->current = current->next;
        } else {
                b->last = current->prev;
        }

        if (current->prev != NULL) {
                current->prev->next = current->next;
                b->current = current->prev;
                b->line -= 1;
        } else {
                b->first = current->next;
        }

        b->size -= str_size(&current->data);
        b->count -= 1;

        str_free(&current->data);
        free(current);
}

struct textbuffer
textbuffer_new(void)
{
        struct line *line = line_new(NULL, str_new(), NULL);
        return (struct textbuffer) {
                .size    = 1,
                .count   = 1,
                .line    = 0,
                .first   = line,
                .current = line,
                .last    = line
        };
}

struct location
textbuffer_insert(struct textbuffer *b, struct location loc, char const *data)
{
        struct location newloc = loc;
        size_t i = strcspn(data, "\n");

        seekline(b, loc.line);
        str_insert_n(&b->current->data, loc.col, i, data);
        newloc.col += i;

        size_t splitoffset = loc.col;
        while (data[i] == '\n') {
                data += i + 1;

                newline(b);
                b->current->data = str_split(&b->current->prev->data, i + splitoffset);

                i = strcspn(data, "\n");
                str_insert_n(&b->current->data, 0, i, data);

                newloc.line += 1;
                newloc.col = i;

                splitoffset = 0;
        }

        return newloc;
}

void
textbuffer_remove(struct textbuffer *b, struct location loc, size_t n)
{
        seekline(b, loc.line);

        size_t remove;
        while (n > (remove = str_size(&b->current->data) - loc.col)) {
                str_truncate(&b->current->data, loc.col);
                b->size -= remove;
                joinline(b);
                n -= (remove + 1);
        }

        str_remove(&b->current->data, loc.col, n);
        b->size -= n;
}

void
textbuffer_remove_line(struct textbuffer *b, size_t i)
{
        seekline(b, i);
        removeline(b);
}

char *
textbuffer_copy_line_cols(struct textbuffer *b, size_t line, char *out, size_t start, size_t n)
{
        seekline(b, line);
        return str_copy_cols(&b->current->data, out, start, n);
}

size_t
textbuffer_num_lines(struct textbuffer *b)
{
        return b->count;
}

size_t
textbuffer_line_width(struct textbuffer *b, size_t line)
{
        seekline(b, line);
        return str_width(&b->current->data);
}

static bool
contentsequal(struct textbuffer const *b, char const *data)
{
        for (struct line *line = b->first; line != NULL; line = line->next) {
                if (str_compare_cstr(&line->data, data) != 0) {
                        return false;
                }
                data += str_size(&line->data);
                if (*data++ != '\n') {
                        return false;
                }
        }

        return true;
}

TEST(create)
{
        struct textbuffer b = textbuffer_new();

        claim(b.count == 1);
        claim(b.line == 0);
        claim(b.current != NULL);
        claim(b.current == b.first);
        claim(b.current == b.last);
}

TEST(insert)
{
        struct textbuffer b = textbuffer_new();

        struct location loc = { 0, 0 };

        loc = textbuffer_insert(&b, loc, "one\ntwo\nthree");
        claim(b.count == 3);

        textbuffer_insert(&b, loc, "\nfour");
        claim(b.count == 4);

        claim(contentsequal(&b, "one\ntwo\nthree\nfour\n"));
}

TEST(remove)
{
        struct textbuffer b = textbuffer_new();

        struct location loc = { 0, 0 };

        textbuffer_insert(&b, loc, "one\ntwo\nthree");

        loc.line = 1;
        loc.col = 2;

        textbuffer_remove(&b, loc, 4);

        claim(contentsequal(&b, "one\ntwree\n"));
}

TEST(remove_line)
{
        struct textbuffer b = textbuffer_new();

        struct location loc = { 0, 0 };

        textbuffer_insert(&b, loc, "one\ntwo\nthree");

        textbuffer_remove_line(&b, 2);
        claim(contentsequal(&b, "one\ntwo\n"));

        textbuffer_remove_line(&b, 0);
        claim(contentsequal(&b, "two\n"));
}

TEST(line_width)
{
        struct textbuffer b = textbuffer_new();

        struct location loc = { 0, 0 };

        textbuffer_insert(&b, loc, "foo\nfoobar\nfoobarbaz");

        claim(textbuffer_line_width(&b, 0) == 3);
        claim(textbuffer_line_width(&b, 1) == 6);
        claim(textbuffer_line_width(&b, 2) == 9);
}
