#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "util.h"
#include "str.h"
#include "panic.h"
#include "alloc.h"
#include "test.h"
#include "location.h"
#include "log.h"
#include "textbuffer.h"

enum {
        EDIT_INSERTION,
        EDIT_DELETION,
        EDIT_GROUP,
};

struct line {
        struct line *prev;
        struct str data;
        struct line *next;
};

struct edit {
        char type;
        union {
                vec(struct edit) edits;
                struct {
                        /*
                         * Note: for deletions, the start and end locations are the same.
                         * For insertions, however, they differ, and this is the _end_ location.
                         */
                        struct location loc;
                        vec(char) data;
                };
        };
};

inline static int
shared_prefix_length(char const *s1, char const *s2, int n)
{
        int spl = 0;

        while (spl < n && s1[spl] == s2[spl]) {
                ++spl;
        }

        return spl;
}

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
        b->lines += 1;
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

        b->bytes -= 1;
        b->lines -= 1;
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

        b->bytes -= str_size(&current->data);
        b->lines -= 1;

        str_free(&current->data);
        free(current);
}

struct textbuffer
textbuffer_new(void)
{
        struct line *line = line_new(NULL, str_new(), NULL);
        struct textbuffer tb = {
                .lines      = 1,
                .line       = 0,
                .bytes      = 0,
                .first      = line,
                .current    = line,
                .last       = line,
        };

        vec_init(tb.history);
        vec_init(tb.markers);
        tb.markers_allocated = 0;

        return tb;
}

int
textbuffer_move_backward(struct textbuffer *b, struct location *loc, int n)
{
        seekline(b, loc->line);

        int most = n;

        n -= str_move_left(&b->current->data, &loc->col, n);
        while (n > 0 && loc->line != 0) {
                --n;
                --loc->line;

                prevline(b);

                loc->col = str_width(&b->current->data);
                n -= str_move_left(&b->current->data, &loc->col, n);
        }

        b->high_col = loc->col;

        return most - n;
}

int
textbuffer_move_forward(struct textbuffer *b, struct location *loc, int n)
{
        seekline(b, loc->line);

        int most = n;

        n -= str_move_right(&b->current->data, &loc->col, n);
        while (n > 0 && loc->line + 1 != b->lines) {
                --n;
                ++loc->line;

                nextline(b);

                loc->col = 0;
                n -= str_move_right(&b->current->data, &loc->col, n);
        }

        b->high_col = loc->col;

        return most - n;
}

int
textbuffer_move_right(struct textbuffer *b, struct location *loc, int n)
{
        seekline(b, loc->line);

        int moved = str_move_right(&b->current->data, &loc->col, n);
        b->high_col = loc->col;

        return moved;
}

int
textbuffer_move_left(struct textbuffer *b, struct location *loc, int n)
{
        seekline(b, loc->line);

        int moved = str_move_left(&b->current->data, &loc->col, n);
        b->high_col = loc->col;

        return moved;
}

int
textbuffer_next_line(struct textbuffer *b, struct location *loc, int n)
{
        seekline(b, loc->line);

        int moving = min(n, b->lines - b->line - 1);

        loc->line += moving;
        seekline(b, loc->line);

        loc->col = str_move_to(&b->current->data, b->high_col);

        return moving;
}

int
textbuffer_prev_line(struct textbuffer *b, struct location *loc, int n)
{
        seekline(b, loc->line);

        int moving = min(n, b->line);

        loc->line -= moving;
        seekline(b, loc->line);

        loc->col = min(str_width(&b->current->data), b->high_col);

        return moving;
}

void
textbuffer_insert(struct textbuffer *b, struct location *loc, char const *data)
{
        textbuffer_insert_n(b, loc, data, strlen(data));
}

/*
 * Insert 'n' _bytes_ from the string pointed to by 'data' into the textbuffer
 * pointed to by 'b' at location 'loc'.
 */
void
textbuffer_insert_n(struct textbuffer *b, struct location *loc, char const *data, int n)
{

        seekline(b, loc->line);

        char const *d = data;
        int bytes = n;

        bool combine_history = (vec_len(b->history) >= 1)
                            && (location_same(vec_last(b->history)->loc, *loc));

        while (n > 0) {

                int i = 0;
                while (i < n && data[i] != '\n') {
                        ++i;
                }

                str_insert_n(&b->current->data, &loc->col, i, data);

                if (i != n) {
                        newline(b);
                        b->current->data = str_split(&b->current->prev->data, loc->col);

                        loc->col = 0;
                        ++loc->line;

                        ++i;
                }

                data += i;
                n -= i;
        }

        b->high_col = loc->col;

        /*
         * Push this edit onto the history stack.
         */
        if (combine_history && vec_last(b->history)->type == EDIT_INSERTION) {
                vec_push_n(vec_last(b->history)->data, d, bytes);
                vec_last(b->history)->loc = *loc;
        } else if (combine_history && vec_last(b->history)->type == EDIT_DELETION) {
                struct edit *del = vec_last(b->history);
                int delcount = del->data.count;
                int n = min(bytes, delcount);
                int spl = shared_prefix_length(d, vec_last(b->history)->data.items, n);
                memmove(del->data.items, del->data.items + spl, delcount - spl);
                del->data.count -= spl;
                if (spl < delcount) {
                        d += spl;
                        bytes -= spl;
                        goto insert;
                }
        } else {
insert:
                {
                        struct edit e = { .type = EDIT_INSERTION, .loc = *loc };
                        vec_init(e.data);
                        vec_push_n(e.data, d, bytes);
                        vec_push(b->history, e);
                }
        }

        b->bytes += n;
}

/*
 * Move the cursor to the end of the buffer and insert n bytes
 * from 'data'.
 */
void
textbuffer_append_n(struct textbuffer *b, struct location *loc, char const *data, int n)
{
        b->current = b->last;
        b->line = b->lines - 1;
        loc->line = b->line;
        textbuffer_insert_n(b, loc, data, n);
}

/*
 * Remove 'n' characters after 'loc'.
 */
int
textbuffer_remove(struct textbuffer *b, struct location loc, int n)
{
        seekline(b, loc.line);

        int most = n;
        int lines_deleted = 0;
        int bytes;
        int chars;

        for (;;) {
                str_remove_chars(&b->current->data, loc.col, n, &bytes, &chars);

                b->bytes -= bytes;
                n -= chars;

                if (b->line + 1 == b->lines || n == 0) {
                        break;
                }

                ++lines_deleted;
                joinline(b);
                --n;
        }


        return most - n;
}

void
textbuffer_remove_line(struct textbuffer *b, int i)
{
        seekline(b, i);
        removeline(b);
}

char *
textbuffer_copy_line_cols(struct textbuffer *b, int line, char *out, int start, int n)
{
        seekline(b, line);
        return str_copy_cols(&b->current->data, out, start, n);
}

int
textbuffer_num_lines(struct textbuffer *b)
{
        return b->lines;
}

int
textbuffer_line_width(struct textbuffer *b, int line)
{
        seekline(b, line);
        return str_width(&b->current->data);
}

char *
textbuffer_get_line(struct textbuffer *b, int line)
{
        seekline(b, line);
        return str_cstr(&b->current->data);
}

void
textbuffer_cut_line(struct textbuffer *b, struct location loc)
{
        seekline(b, loc.line);
        textbuffer_remove(b, loc, str_width(&b->current->data) - loc.col);
}

void
textbuffer_delete_marker(struct textbuffer *b, struct location *m)
{
        // TODO
}

struct location const *
textbuffer_new_marker(struct textbuffer *b, struct location loc)
{
        if (b->markers.count == b->markers_allocated) {
                struct location *m = alloc(sizeof *m);
                *m = loc;
                vec_push(b->markers, m);
                ++b->markers_allocated;
                return m;
        } else {
                *b->markers.items[b->markers.count++] = loc;
                return *vec_last(b->markers);
        }
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

        claim(b.bytes == 0);
        claim(b.line == 0);
        claim(b.current != NULL);
        claim(b.current == b.first);
        claim(b.current == b.last);
}

TEST(insert)
{
        struct textbuffer b = textbuffer_new();

        struct location loc = { 0, 0 };

        textbuffer_insert(&b, &loc, "one\ntwo\nthree");
        claim(b.lines == 3);

        textbuffer_insert(&b, &loc, "\nfour");
        claim(b.lines == 4);

        claim(contentsequal(&b, "one\ntwo\nthree\nfour\n"));
}

TEST(remove)
{
        struct textbuffer b = textbuffer_new();

        struct location loc = { 0, 0 };

        textbuffer_insert(&b, &loc, "one\ntwo\nthree");

        loc.line = 1;
        loc.col = 2;

        textbuffer_remove(&b, loc, 4);

        claim(contentsequal(&b, "one\ntwree\n"));
}

TEST(remove_line)
{
        struct textbuffer b = textbuffer_new();

        struct location loc = { 0, 0 };

        textbuffer_insert(&b, &loc, "one\ntwo\nthree");

        textbuffer_remove_line(&b, 2);
        claim(contentsequal(&b, "one\ntwo\n"));

        textbuffer_remove_line(&b, 0);
        claim(contentsequal(&b, "two\n"));
}

TEST(line_width)
{
        struct textbuffer b = textbuffer_new();

        struct location loc = { 0, 0 };

        textbuffer_insert(&b, &loc, "foo\nfoobar\nfoobarbaz");

        claim(textbuffer_line_width(&b, 0) == 3);
        claim(textbuffer_line_width(&b, 1) == 6);
        claim(textbuffer_line_width(&b, 2) == 9);
}

TEST(move_backward)
{
        struct textbuffer b = textbuffer_new();

        struct location loc = { 0, 0 };

        textbuffer_insert(&b, &loc, "line one\nline two");

        struct location c = { 1, 2 }; // just before the 'n' in "line two"
        claim(textbuffer_move_backward(&b, &c, 4) == 4);

        claim(c.line == 0);
        claim(c.col == 7);
}

TEST(insert_n)
{
        struct textbuffer b = textbuffer_new();

        struct location loc = { 0, 0 };

        textbuffer_insert(&b, &loc, "line one\nline two");

        struct location c = { 1, 2 }; // just before the 'n' in "line two"

        char data[] = { 'A', 'B', 'C' };
        textbuffer_insert_n(&b, &c, data, 3);

        claim(contentsequal(&b, "line one\nliABCne two\n"));
}

TEST(history_combined)
{
        struct textbuffer b = textbuffer_new();

        struct location loc = { 0, 0 };

        textbuffer_insert(&b, &loc, "one\ntwo\nthree");
        claim(b.lines == 3);

        textbuffer_insert(&b, &loc, "\nfour");
        claim(b.lines == 4);

        claim(contentsequal(&b, "one\ntwo\nthree\nfour\n"));

        printf("history length = %d\n", (int) vec_len(b.history));
        claim(vec_len(b.history) == 1);
        claim(vec_get(b.history, 0)->type == EDIT_INSERTION);

        char const *inserted = "one\ntwo\nthree\nfour";
        claim(strncmp(inserted, vec_get(b.history, 0)->data.items, strlen(inserted)) == 0);
}

TEST(large_history)
{
        struct textbuffer b = textbuffer_new();
        struct location loc = { 0, 0 };

        for (int i = 0; i < 1000000; ++i) {
                textbuffer_insert(&b, &loc, "one\ntwo\nthree");
        }

        printf("count = %d\n", (int) b.history.count);
        claim(b.history.count == 1);
        claim(b.history.items[0].data.count > 10000000);
}
