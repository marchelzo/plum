#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

#include <limits.h>
#include <time.h>

#include <pcre.h>
#include <fcntl.h>
#include <unistd.h>

#include "tb.h"
#include "log.h"
#include "test.h"
#include "util.h"
#include "value.h"
#include "buffer.h"
#include "panic.h"
#include "alloc.h"
#include "utf8.h"
#include "vm.h"

#define RIGHT(s) ((s)->right + (s)->capacity - (s)->rightcount)
#define CURRENT_EDIT(s) (&(s)->edits.items[(s)->edits.count - 1])
#define LAST_CHANGE(e) (&(e)->changes.items[(e)->changes.count - 1])

enum {
        INITIAL_CAPACITY = 256,
};

/*
 * This struct represents a single atomic modification to the buffer.
 */
struct change {
        enum { CHANGE_INSERTION, CHANGE_DELETION } type;
        vec(char) data;
        int bytes;
        int characters;
        int where;
};

/*
 * A logical group of edits. Undo will undo an entire group at
 * a time.
 */
struct edit {
        struct tm when;
        vec(struct change) changes;
};

static struct stringpos limitpos;
static struct stringpos outpos;

/*
 * If necessary, grow the left and right buffers in s so that
 * they are both at least n bytes.
 */
inline static void
grow(struct tb *s, int n)
{
        if (s->capacity >= n)
                return;

        int oldcapacity = s->capacity;

        while (s->capacity < n)
                s->capacity *= 2;

        resize(s->left, s->capacity);
        resize(s->right, s->capacity);

        memmove(s->right + s->capacity - s->rightcount, s->right + oldcapacity - s->rightcount, s->rightcount);
}

inline static void
count(char const *s, int n)
{
        utf8_count(s, n, &outpos);
}

/*
 * Copy text from 'str' into the text buffer. str points into a string
 * of 'len' bytes, but the number of bytes copied into the buffer may
 * not be exactly len. Control characters other bytes that we can't decode
 * as UTF-8 are stripped, and \t is expanded into several spaces.
 */
inline static void
insert(struct tb *s, char const *str, int len)
{
        uint32_t cp;
        int bytes;
        int width;

        enum { TabWidth = 8 };

        while (len != 0) switch (str[0]) {
        case '\n':
                str += 1;
                len -= 1;

                s->lines += 1;
                s->line += 1;
                s->column = 0;
                s->characters += 1;
                s->character += 1;
                s->left[s->leftcount++] = '\n';

                continue;
        case '\t':
                str += 1;
                len -= 1;

                width = TabWidth - (s->column % TabWidth);
                grow(s, s->leftcount + s->rightcount + width);

                s->characters += width;
                s->character += width;
                s->column += width;

                while (width --> 0)
                        s->left[s->leftcount++] = ' ';

                continue;
        default:
                bytes = next_utf8(str, len, &cp);
                if (bytes == -1) {
                        str += 1;
                        len -= 1;
                        continue;
                }

                /* Skip C0 or C1 controls */
                if (cp < 0x20 || (cp >= 0x80 && cp < 0xa0))
                        goto Skip;

                width = mk_wcwidth(cp);
                if (width == -1)
                        goto Skip;

                s->column += width;
                if (width > 0) {
                        s->characters += 1;
                        s->character += 1;
                }

                memcpy(s->left + s->leftcount, str, bytes);
                s->leftcount += bytes;
Skip:
                str += bytes;
                len -= bytes;

                continue;
        }
}

inline static void
stringcount(char const *s, int byte_lim, int col_lim, int grapheme_lim, int line_lim)
{
        limitpos.bytes = byte_lim;
        limitpos.columns = col_lim;
        limitpos.graphemes = grapheme_lim;
        limitpos.lines = line_lim;
        utf8_stringcount(s, byte_lim, &outpos, &limitpos);
}

inline static void
seekforward(struct tb *s, int n)
{
        char const *r = RIGHT(s);

        stringcount(r, n, -1, -1, -1);

        s->column = outpos.column + ((outpos.lines >= 1) ? 0 : s->column);
        s->character += outpos.graphemes;

        memcpy(s->left + s->leftcount, r, outpos.bytes);
        s->leftcount += outpos.bytes;
        s->rightcount -= outpos.bytes;
}

inline static void
seekbackward(struct tb *s, int n)
{
        char const *l = s->left + s->leftcount - n;

        stringcount(l, n, -1, -1, -1);

        s->character -= outpos.graphemes;

        memcpy(s->right + s->capacity - s->rightcount - outpos.bytes, l, outpos.bytes);
        s->leftcount -= outpos.bytes;
        s->rightcount += outpos.bytes;

        char const *start = l;
        while (start != s->left && start[-1] != '\n')
                --start;

        s->column = utf8_columncount(start, l - start);
}

inline static char const *
getlineptr(struct tb const *s, int line)
{
        if (line == 0)
                return s->left;

        int dy = s->line - line;
        char const *p = s->left + s->leftcount;
        while (dy > 0) {
                if (p[-1] == '\n')
                        --dy;
                --p;
        }

        while (p > s->left && p[-1] != '\n')
                --p;

        return p;
}

inline static void
deledit(struct edit *e)
{
        for (int i = 0; i < e->changes.count; ++i) {
                e->changes.items[i].bytes = 0;
                e->changes.items[i].characters = 0;
                vec_empty(e->changes.items[i].data);
        }

        vec_empty(e->changes);
}

inline static void
seekhighcol(struct tb *s)
{
        int hc = s->highcol;
        tb_right(s, s->highcol);
        s->highcol = hc;
}

inline static void
seekendline(struct tb *s)
{
        char const *r = s->right + s->capacity - s->rightcount;

        int i;
        for (i = 0; i < s->rightcount; ++i)
                if (r[i] == '\n')
                        break;

        stringcount(r, i, -1, -1, -1);
        s->column += outpos.columns;
        s->character += outpos.graphemes;

        memcpy(s->left + s->leftcount, r, outpos.bytes);
        s->leftcount += outpos.bytes;
        s->rightcount -= outpos.bytes;
}

inline static void
record(struct tb *s, int type, char const *data, int bytes, int characters, int where)
{
        /*
         * If we're starting a new edit, move the history index up
         * and delete everything after it (things that were undone).
         */
        if (s->history_index + 1 < s->edits.count) {
                ++s->history_index;
                for (int i = s->history_index; i < s->edits.count; ++i)
                        deledit(&s->edits.items[i]);
                s->edits.count = s->history_index + 1;
        }

        struct edit *c = CURRENT_EDIT(s);

        s->history_usage += bytes;

        switch (type) {
        case CHANGE_INSERTION:
                if (c->changes.count >= 1) {
                        /* Maybe we can append this data directly to the last change */
                        struct change *last = LAST_CHANGE(c);
                        if (last->type == CHANGE_INSERTION && last->where + last->characters == where) {
                                last->bytes += bytes;
                                last->characters += characters;
                                vec_reserve(last->data, last->bytes);
                                vec_push_n(last->data, data, bytes);
                                return;
                        } else if (last->type == CHANGE_DELETION && last->where == where) {
                                /*
                                 * The last change was a deletion at the same location, so if this insertion
                                 * has a common prefix with it, we can just cancel them.
                                 */
                                int maxprefix = min(bytes, last->bytes);
                                int common = 0;
                                while (common < maxprefix && data[common] == last->data.items[common])
                                        ++common;

                                if (common == 0)
                                        break;

                                /* special case for single-byte insertions (no need to call utf8_charcount) */
                                if (bytes == 1) {
                                        memmove(last->data.items, last->data.items + 1, last->bytes);
                                        --last->data.count;
                                        --last->bytes;
                                        --last->characters;
                                        ++last->where;
                                        return;
                                }

                                /* update the delete info to reflect the cancellation */
                                memmove(last->data.items, last->data.items + common, last->bytes);
                                int chars = utf8_charcount(data, common);
                                last->data.count -= common;
                                last->bytes -= common;
                                last->characters -= chars;
                                last->where += chars;

                                /* maybe the entire insertion was a prefix of the deletion */
                                if (bytes == common)
                                        return;

                                bytes -= common;
                                data += common;
                                characters -= chars;
                                where += chars;
                        }
                }
                break;
        case CHANGE_DELETION:
                if (c->changes.count >= 1) {
                        /* Maybe we can group this directly with the last deletion */
                        struct change *last = LAST_CHANGE(c);
                        if (last->type == CHANGE_DELETION && where + characters == last->where) {
                                last->where = where;
                                last->bytes += bytes;
                                last->characters += characters;
                                vec_reserve(last->data, last->bytes);
                                memmove(last->data.items + bytes, last->data.items, last->bytes);
                                memcpy(last->data.items, data, bytes);
                                last->data.count += bytes;
                                return;
                        } else if (last->type == CHANGE_DELETION && last->where == where) {
                                last->bytes += bytes;
                                last->characters += characters;
                                vec_reserve(last->data, last->bytes);
                                vec_push_n(last->data, data, bytes);
                                return;
                        } else if (last->type == CHANGE_INSERTION && last->where + last->characters == where + characters) {
                                /*
                                 * This deletion ends in the same place the previous edit (an insertion)
                                 * ended. This means that one of them is a suffix of the other, and we can
                                 * just do a cancellation.
                                 */
                                if (bytes > last->bytes) {
                                        /* the deletion completely negates the insertion */
                                        last->type = CHANGE_DELETION;
                                        last->bytes = bytes - last->bytes;
                                        last->characters = characters - last->characters;
                                        last->where = where;
                                        vec_reserve(last->data, last->bytes);
                                        memcpy(last->data.items, data, last->bytes);
                                        last->data.count = last->bytes;
                                } else {
                                        /* extremely common case. this is what happens on backspace */
                                        last->data.count -= bytes;
                                        last->bytes -= bytes;
                                        last->characters -= characters;
                                }

                                return;
                        }
                }
                break;
        }

        struct change newchange;
        newchange.type = type;
        newchange.where = where;
        newchange.bytes = bytes;
        newchange.characters = characters;
        vec_init(newchange.data);
        vec_reserve(newchange.data, bytes);
        vec_push_n(newchange.data, data, bytes);
        vec_push(c->changes, newchange);
}

inline static void
seek(struct tb *s, int i)
{
        if (i == s->character) {
                return;
        }
        
        char *r = RIGHT(s);

        if (i > s->character) {
                stringcount(r, s->rightcount, -1, i - s->character, -1);
                memcpy(s->left + s->leftcount, r, outpos.bytes);
                s->leftcount += outpos.bytes;
                s->rightcount -= outpos.bytes;
                s->line += outpos.lines;
        } else if (i < s->character / 2) {
                stringcount(s->left, s->leftcount, -1, i, -1);
                memcpy(r - s->leftcount + outpos.bytes, s->left + outpos.bytes, s->leftcount - outpos.bytes);
                s->rightcount += s->leftcount - outpos.bytes;
                s->leftcount = outpos.bytes;
                s->line = outpos.lines;
        } else {
                tb_backward(s, s->character - i);
                return;
        }

        s->column = outpos.column;
        s->character = i;
}

void
tb_seek_line(struct tb *s, int i)
{
        if (i >= s->lines)
                i = s->lines - 1;

        if (i < 0)
                i = 0;

        int ln = i;
        int bytes = 0;

        if (i >= s->line) {
                i -= s->line;
                char const *r = RIGHT(s);
                char const *end = s->right + s->capacity;
                while (i > 0 && r < end)
                        ++bytes, i -= *r++ == '\n';
                s->character += utf8_charcount(RIGHT(s), bytes);
                memcpy(s->left + s->leftcount, RIGHT(s), bytes);
                s->leftcount += bytes;
                s->rightcount -= bytes;
        } else if (i <= s->line / 2) {
                bytes = s->leftcount;
                char const *l = s->left;
                while (i > 0)
                        --bytes, i -= *l++ == '\n';
                s->character = utf8_charcount(s->left, bytes);
                memcpy(RIGHT(s) - bytes, l, bytes);
                s->leftcount -= bytes;
                s->rightcount += bytes;
        } else {
                i = s->line - i;
                char const *l = s->left + s->leftcount;
                while (i > 0)
                        ++bytes, i -= *--l == '\n';
                while (l > s->left && l[-1] != '\n')
                        ++bytes, --l;
                s->character -= utf8_charcount(l, bytes);
                memcpy(RIGHT(s) - bytes, l, bytes);
                s->leftcount -= bytes;
                s->rightcount += bytes;
        }

        s->line = ln;
        s->highcol = s->column = 0;
}

inline static int
removen(struct tb *s, int n, bool should_record)
{
        char const *r = RIGHT(s);

        stringcount(r, s->rightcount, -1, n, -1);
        s->rightcount -= outpos.bytes;
        s->lines -= outpos.lines;
        s->characters -= outpos.graphemes;

        for (int i = 0; i < s->markers.count; ++i) {
                int *m = s->markers.items[i];
                if (*m > s->character)
                        *m -= outpos.graphemes;
        }

        if (should_record) {
                record(s, CHANGE_DELETION, r, outpos.bytes, outpos.graphemes, s->character);
                s->changed = true;
        }

        return outpos.graphemes;
}

inline static void
pushn(struct tb *s, char const *data, int n, bool should_record)
{
        grow(s, s->leftcount + s->rightcount + n);

        int lc = s->leftcount;
        int c = s->character;
        insert(s, data, n);

        /* Update markers and history before changing s->character */
        for (int i = 0; i < s->markers.count; ++i) {
                int *m = s->markers.items[i];
                if (*m > c)
                        *m += (s->character - c);
        }
        
        if (should_record) {
                record(
                        s,
                        CHANGE_INSERTION,
                        s->left + lc,
                        s->leftcount - lc,
                        s->character - c,
                        c
                );
                s->changed = true;
        }
}


inline static void
undochange(struct tb *s, struct change const *c)
{
        seek(s, c->where);

        switch (c->type) {
        case CHANGE_INSERTION:
                removen(s, c->characters, false);
                break;
        case CHANGE_DELETION:
                pushn(s, c->data.items, c->bytes, false);
                break;
        }
}

inline static void
redochange(struct tb *s, struct change const *c)
{
        seek(s, c->where);

        switch (c->type) {
        case CHANGE_INSERTION:
                pushn(s, c->data.items, c->bytes, false);
                break;
        case CHANGE_DELETION:
                removen(s, c->characters, false);
                break;
        }

        // TODO: optimize this
        seek(s, c->where);
}

inline static void
undo(struct tb *s, struct edit const *e)
{
        for (int i = e->changes.count - 1; i >= 0; --i) {
                undochange(s, &e->changes.items[i]);
        }
}

inline static void
redo(struct tb *s, struct edit const *e)
{
        for (int i = 0; i < e->changes.count; ++i) {
                redochange(s, &e->changes.items[i]);
        }
}

inline static void
free_edit(struct edit *e)
{
        for (int i = 0; i < e->changes.count; ++i)
                vec_empty(e->changes.items[i].data);
}

struct tb
tb_new(void)
{
        struct tb s = {
                .left       = alloc(INITIAL_CAPACITY + 1),
                .right      = alloc(INITIAL_CAPACITY + 1),
                .leftcount  = 0,
                .rightcount = 0,
                .capacity   = INITIAL_CAPACITY,
                .characters = 0,
                .character  = 0,
                .lines      = 0,
                .line       = 0,
                .changed    = false
        };

        vec_init(s.markers);
        s.markers_allocated = 0;

        vec_init(s.edits);
        s.record_history = false;
        s.history_usage = 0;
        s.history_index = -1;

        return s;
}

void
tb_clear(struct tb *s)
{
        seek(s, 0);
        removen(s, s->characters, s->record_history);
}

void
tb_clear_left(struct tb *s)
{
        int n = s->character;
        seek(s, 0);
        removen(s, n, s->record_history);
}

void
tb_clear_right(struct tb *s)
{
        removen(s, s->characters - s->character, s->record_history);
}

/*
 * Free all memory used by the tb. This can leave
 * the tb in an invalid state.
 */
void
tb_murder(struct tb *s)
{
        free(s->left);
        free(s->right);

        for (int i = 0; i < s->markers.count; ++i)
                free(s->markers.items[i]);
        vec_empty(s->markers);

        for (int i = 0; i < s->edits.count; ++i)
                free_edit(&s->edits.items[i]);
        vec_empty(s->edits);
}

int
tb_seek(struct tb *s, int i)
{
        seek(s, i);
        s->highcol = s->column;
        return s->character;
}

/*
 * Insert n bytes.
 */
void
tb_insert(struct tb *s, char const *data, int n)
{
        pushn(s, data, n, s->record_history);
        s->highcol = s->column;
}

/*
 * Append to the end of the buffer without moving the cursor.
 */
void
tb_append(struct tb *s, char const *data, int n)
{
        grow(s, s->leftcount + s->rightcount + n);
        insert(s, data, n);
        count(data, n);

        char *r = RIGHT(s);
        memmove(r - n, r, s->rightcount);
        memcpy(s->right + s->capacity - n, data, n);

        if (s->record_history) {
                record(s, CHANGE_INSERTION, data, n, outpos.graphemes, s->characters);
                s->changed = true;
        }

        s->rightcount += n;
        s->lines += outpos.lines;
        s->characters += outpos.graphemes;
}

/*
 * Append to the end of the buffer without moving the cursor
 * while also adding a newline. This is much faster than doing
 * tb_append(&tb, data, n); tb_append(&td, "\n", 1); because
 * each tb_append requires a memmove of all of the bytes on
 * the right of the gap.
 */
void
tb_append_line(struct tb *s, char const *data, int n)
{
        grow(s, s->leftcount + s->rightcount + n + 1);
        count(data, n);

        char *r = RIGHT(s);
        memmove(r - n - 1, r, s->rightcount);
        memcpy(s->right + s->capacity - n - 1, data, n);
        r[s->rightcount - 1] = '\n';

        if (s->record_history) {
                record(s, CHANGE_INSERTION, data, n, outpos.graphemes, s->characters);
                record(s, CHANGE_INSERTION, "\n", 1, 1, s->characters + outpos.graphemes);
                s->changed = true;
        }
        
        s->rightcount += n + 1;
        s->lines += outpos.lines + 1;
        s->characters += outpos.graphemes + 1;
}

int
tb_read(struct tb *s, int fd)
{
        static char buf[4096];

        int n;
        while (n = read(fd, buf, sizeof buf), n > 0)
                pushn(s, buf, n, s->record_history);

        return (n == 0) ? 0 : -1;
}

int
tb_write(struct tb const *s, int fd)
{
        char const *r = RIGHT(s);

        // TODO handle write errors
        write(fd, s->left, s->leftcount);
        write(fd, RIGHT(s), s->rightcount);

        int n = s->leftcount + s->rightcount;
        if (n == 0)
                return n;
        if (s->rightcount == 0 && s->left[n - 1] == '\n')
                return n;
        if (r[s->rightcount - 1] == '\n')
                return n;

        write(fd, "\n", 1);

        return n + 1;
}

/*
 * Remove n characters.
 */
int
tb_remove(struct tb *s, int n)
{
        return removen(s, n, s->record_history);
}

int
tb_size(struct tb const *s)
{
        return s->leftcount + s->rightcount;
}

void
tb_draw(struct tb const *s, char *out, int line, int col, int lines, int cols)
{
        char const *lptr = getlineptr(s, line);

        int drawing = min(lines, tb_lines(s) - line);
        out = writeint(out, drawing);

        /*
         * First draw all of the lines before the line the cursor is on.
         */
        for (int i = line; i < s->line; ++i) {
                out = utf8_copy_cols(lptr, INT_MAX, out, col, cols);
                while (*lptr != '\n') ++lptr;
                ++lptr;
        }

        /* 
         * Draw the line with the cursor on it.
         */
        {
                char const *r = RIGHT(s);
                int moved = 0;

                while (moved < s->rightcount && *r != '\n')
                        s->left[s->leftcount + moved++] = *r++;

                out = utf8_copy_cols(lptr, s->left + s->leftcount - lptr + moved, out, col, cols);

                if (s->line == s->lines || moved == s->rightcount) {
                        lptr = r + s->rightcount;
                        return;
                }

                lptr = r + 1;
        }

        /*
         * Finally draw all of the lines after the line the cursor is on.
         */
        int maxline = min(line + lines - 1, s->lines);
        char const *end = s->right + s->capacity;
        for (int i = s->line; i < maxline; ++i) {
                out = utf8_copy_cols(lptr, s->right + s->capacity - lptr, out, col, cols);
                while (lptr < end && *lptr != '\n')
                        ++lptr;
                if (lptr != end)
                        ++lptr;
        }
}


static int
tb_compare_cstr(struct tb const *s, char const *cstr)
{
        int n = strlen(cstr);

        int k = strncmp(s->left, cstr, s->leftcount);

        if (k != 0) {
                return k;
        }
        if (n < s->leftcount) {
                return 1;
        }

        return strncmp(RIGHT(s), cstr + s->leftcount, s->rightcount);
}

char *
tb_cstr(struct tb const *s)
{
        char *cstr = alloc(s->leftcount + s->rightcount + 1);
        memcpy(cstr, s->left, s->leftcount);
        memcpy(cstr + s->leftcount, RIGHT(s), s->rightcount);
        cstr[s->leftcount + s->rightcount] = '\0';
        return cstr;
}

int
tb_up(struct tb *s, int n)
{
        if (n == 0 || s->line == 0)
                return 0;

        n = min(n, s->line);

        char const *p = s->left + s->leftcount;
        while (n > 0)
                if (*--p == '\n')
                        --n;
        while (p != s->left && p[-1] != '\n')
                --p;

        char *r = RIGHT(s);
        count(p, s->left + s->leftcount - p);
        memcpy(r - outpos.bytes, p, outpos.bytes);

        s->column = 0;
        s->line -= outpos.lines;
        s->character -= outpos.graphemes;
        s->leftcount -= outpos.bytes;
        s->rightcount += outpos.bytes;

        seekhighcol(s);

        return n;
}

int
tb_forward(struct tb *s, int n)
{
        int move = min(n, s->characters - s->character);

        char *r = RIGHT(s);
        stringcount(r, s->rightcount, -1, n, -1);
        memcpy(s->left + s->leftcount, r, outpos.bytes);

        s->column = outpos.column + ((outpos.lines >= 1) ? 0 : s->column);
        s->leftcount += outpos.bytes;
        s->rightcount -= outpos.bytes;
        s->character += outpos.graphemes;
        s->line += outpos.lines;

        s->highcol = s->column;

        return move;
}

int
tb_right(struct tb *s, int n)
{
        char const *r = RIGHT(s);

        if (s->line == s->lines) {
                stringcount(r, s->rightcount, -1, n, -1);
        } else {
                int i = 0;
                while (r[i] != '\n')
                        ++i;
                stringcount(r, i, -1, n, -1);
        }

        memcpy(s->left + s->leftcount, r, outpos.bytes);
        s->leftcount += outpos.bytes;
        s->rightcount -= outpos.bytes;
        s->column += outpos.columns;
        s->character += outpos.graphemes;

        s->highcol = s->column;

        return n - outpos.graphemes;
}

int
tb_left(struct tb *s, int n)
{
        char const *l = s->left + s->leftcount;
        int back = 0;
        while (l > s->left && l[-1] != '\n')
                --l, ++back;

        int ch = utf8_charcount(l, back);

        stringcount(l, s->leftcount, -1, max(ch - n, 0), -1);
        int move = ch - outpos.graphemes;

        int copy = back - outpos.bytes;
        memcpy(RIGHT(s) - copy, s->left + s->leftcount - copy, copy);
        s->leftcount -= copy;
        s->rightcount += copy;
        s->column = outpos.columns;
        s->character -= move;

        s->highcol = s->column;

        return move;
}

void
tb_start_of_line(struct tb *s)
{
        char const *l = s->left + s->leftcount;
        int back = 0;
        while (l > s->left && l[-1] != '\n')
                --l, ++back;

        int chars = utf8_charcount(l, back);

        memcpy(RIGHT(s) - back, s->left + s->leftcount - back, back);
        s->leftcount -= back;
        s->rightcount += back;
        s->column = 0;
        s->character -= chars;

        s->highcol = 0;
}

void
tb_end_of_line(struct tb *s)
{
        int i = 0;
        char const *r = RIGHT(s);
        while (i < s->rightcount && r[i] != '\n')
                ++i;

        count(r, i);

        memcpy(s->left + s->leftcount, r, i);
        s->leftcount += outpos.bytes;
        s->rightcount -= outpos.bytes;
        s->column += outpos.columns;
        s->character += outpos.graphemes;
        s->highcol = s->column;
}

int
tb_backward(struct tb *s, int n)
{
        int move = min(n, s->character);

        /*
         * This is pretty much guaranteed to be far enough back, right?
         *
         *   ...right?
         *
         * This should probably be optimized for the case where we're only
         * going backward over ASCII characters.
         */
        int back = min(move * 8, s->leftcount);
        char const *p = s->left + s->leftcount - back;

        while (p > s->left && p[-1] != '\n')
                --p, ++back;

        /* TODO: just count newlines */
        count(p, back);
        int maxlines = outpos.lines;

        stringcount(p, back, -1, outpos.graphemes - move, -1);

        int dy = maxlines - outpos.lines;
        int copy = back - outpos.bytes;

        memcpy(RIGHT(s) - copy, s->left + s->leftcount - copy, copy);

        s->leftcount -= copy;
        s->rightcount += copy;
        s->column = outpos.column;
        s->line -= dy;
        s->character -= move;

        s->highcol = s->column;
        
        return move;
}

int
tb_down(struct tb *s, int n)
{
        if (n == 0 || s->line == s->lines)
                return 0;

        char const *r = RIGHT(s);
        stringcount(r, s->rightcount, -1, -1, n);
        memcpy(s->left + s->leftcount, r, outpos.bytes);

        s->leftcount += outpos.bytes;
        s->rightcount -= outpos.bytes;
        s->column = outpos.column;
        s->line += outpos.lines;
        s->character += outpos.graphemes;

        seekhighcol(s);
        
        return n - outpos.lines;
}

int
tb_truncate_line(struct tb *s)
{
        int deleted;

        if (s->line == s->lines) {
                deleted = s->characters - s->character;
                removen(s, deleted, s->record_history);
        } else {
                char const *r = RIGHT(s);
                stringcount(r, s->rightcount, -1, -1, 1);

                /* We don't want to delete the newline. */
                --outpos.bytes;
                --outpos.graphemes;

                deleted = outpos.graphemes;

                s->rightcount -= outpos.bytes;
                s->characters -= outpos.graphemes;

                if (s->record_history) {
                        record(s, CHANGE_DELETION, r, outpos.bytes, outpos.graphemes, s->character);
                        s->changed = true;
                }
        }

        return deleted;
}

void
tb_start(struct tb *s)
{
        memcpy(s->right + s->capacity - s->rightcount - s->leftcount, s->left, s->leftcount);
        s->line = 0;
        s->character = 0;
        s->column = 0;
        s->rightcount += s->leftcount;
        s->leftcount = 0;
}

void
tb_end(struct tb *s)
{
        memcpy(s->left + s->leftcount, RIGHT(s), s->rightcount);
        s->line = s->lines;
        s->character = s->characters;
        s->leftcount += s->rightcount;
        s->rightcount = 0;

        int i = s->leftcount;
        while (i > 0 && s->left[i - 1] != '\n')
                --i;

        s->column = utf8_columncount(s->left + i, s->leftcount - i);
}

/*
 * It's assumed here that i is non-negative. This should be checked
 * in editor_functions.c
 */
struct value
tb_get_char(struct tb const *s, int i)
{
        if (i >= s->characters) {
                return NIL;
        }

        int bytes;
        char const *c;
        if (i < s->character)
                c = utf8_nth_char(s->left, i, &bytes);
        else
                c = utf8_nth_char(RIGHT(s), i - s->character, &bytes);

        return STRING_CLONE(c, bytes);
}

/*
 * Find the next occurrence of a string on the current line.
 */
bool
tb_find_next(struct tb *s, char const *c, int n)
{
        char const *r = RIGHT(s);
        char const *end = r + s->rightcount;

        if (s->rightcount == 0 || r[0] == '\n')
                return false;

        for (char const *rp = utf8_next_char(r, s->rightcount); *rp != '\n' && rp != end; ++rp) {
                if (end - rp >= n && strncmp(rp, c, n) == 0) {
                        seekforward(s, rp - r);
                        s->highcol = s->column;
                        return true;
                }
        }

        return false;
}

/*
 * Find the previous occurrence of a string on the current line.
 */
bool
tb_find_prev(struct tb *s, char const *c, int n)
{
        char const *l = s->left + s->leftcount;
        char const *end = l;

        while (l != s->left && l[-1] != '\n')
                --l;

        char const *prev = NULL;

        while (l != end && l[0] != '\n') {
                if (end - l >= n && strncmp(l, c, n) == 0) {
                        prev = l;
                        l += n;
                } else {
                        ++l;
                }
        }

        if (prev == NULL)
                return false;

        seekbackward(s, end - prev);
        s->highcol = s->column;

        return true;

}

void
tb_each_line(struct tb const *s, struct value *f)
{
        f->refs->mark |= GC_HARD;

        int ln = 0;
        char const *l = s->left;
        char const *end = s->left + s->leftcount;

        for (char const *start = l; l != end; ++l) {
                if (*l != '\n') 
                        continue;

                struct value line = STRING_CLONE(start, l - start);
                vm_eval_function2(f, &line, &INTEGER(ln));

                ++ln;
                start = l + 1;
        }

        struct value current = tb_get_line(s, s->line);
        vm_eval_function2(f, &current, &INTEGER(ln));

        ++ln;

        char const *r = RIGHT(s);
        end = r + s->rightcount;
        char const *start;
        for (start = r; r != end; ++r) {
                if (*r != '\n') 
                        continue;

                struct value line = STRING_CLONE(start, r - start);
                vm_eval_function2(f, &line, &INTEGER(ln));

                ++ln;
                start = r + 1;
        }

        struct value line = STRING_CLONE(start, r - start);
        vm_eval_function2(f, &line, &INTEGER(ln));

        f->refs->mark &= ~GC_HARD;
}

struct value
tb_get_line(struct tb const *s, int i)
{
        if (i > s->lines)
                return NIL;

        if (i == s->line) {
                int lb = 0;
                char const *l = s->left + s->leftcount;
                while (l != s->left && l[-1] != '\n')
                        --l, ++lb;

                int rb = 0;
                char const *r = RIGHT(s);
                while (rb < s->rightcount && r[rb] != '\n')
                        ++rb;

                struct string *line = value_string_alloc(lb + rb);
                memcpy(line->data, l, lb);
                memcpy(line->data + lb, r, rb);

                return STRING(line->data, lb + rb, line);
        }

        char const *p;

        if (i < s->line) {
                p = s->left;
        } else {
                i -= s->line;
                p = RIGHT(s);
        }

        while (i != 0)
                if (*p++ == '\n')
                        --i;

        int bytes = 0;
        while (p[bytes] != '\n')
                ++bytes;

        return STRING_CLONE(p, bytes);
}

char *
tb_clone_line(struct tb const *s)
{
        int leftbytes = 0;
        char const *l = s->left + s->leftcount;
        while (l > s->left &&  l[-1] != '\n')
                --l, ++leftbytes;

        char const *r = RIGHT(s);
        int rightbytes = 0;
        while (rightbytes < s->rightcount && r[rightbytes] != '\n')
                ++rightbytes;

        char *line = alloc(leftbytes + rightbytes + 1);
        memcpy(line, l, leftbytes);
        memcpy(line + leftbytes, r, rightbytes);
        line[leftbytes + rightbytes] = '\0';
        
        return line;
}

int *
tb_new_marker(struct tb *s, int pos)
{
        int *marker;

        if (s->markers.count == s->markers_allocated) {
                marker = alloc(sizeof *marker);
                vec_push(s->markers, marker);
                ++s->markers_allocated;
        } else {
                marker = s->markers.items[s->markers.count++];
        }

        *marker = pos;

        return marker;
}

void
tb_delete_marker(struct tb *s, int *marker)
{
        int **lastptr = vec_last(s->markers);
        int *last = *lastptr;

        for (int i = 0; i < s->markers.count; ++i) {
                if (s->markers.items[i] == marker) {
                        *lastptr = marker;
                        s->markers.items[i] = last;
                        --s->markers.count;
                        return;
                }
        }
}

void
tb_start_new_edit(struct tb *s)
{
        if (s->edits.count == s->history_index + 1) {
                ++s->edits.count;
                vec_reserve(s->edits, s->edits.count);
        }

        struct edit *e = vec_last(s->edits);

        time_t t = time(NULL);
        localtime_r(&t, &e->when);
        vec_init(e->changes);
}

/*
 * If there is an edit to undo, undo it and return true.
 * Otherwise return false.
 */
bool
tb_undo(struct tb *s)
{
        if (s->history_index == -1)
                return false;

        undo(s, &s->edits.items[s->history_index--]);

        return true;
}

/*
 * If there is an edit to redo, redo it and return true.
 * Otherwise return false.
 */
bool
tb_redo(struct tb *s)
{
        if (s->history_index + 1 == s->edits.count || s->edits.items[s->history_index + 1].changes.count == 0)
                return false;

        redo(s, &s->edits.items[++s->history_index]);

        return true;
}

int
tb_line_width(struct tb *s)
{
        int lb = 0;
        char const *l = s->left + s->leftcount;
        while (l != s->left && l[-1] != '\n')
                --l, ++lb;

        int rb = 0;
        char const *r = RIGHT(s);
        while (rb < s->rightcount && r[rb] != '\n')
                ++rb;

        return utf8_columncount(l, lb) + utf8_columncount(r, rb);
}

bool
tb_next_match_regex(struct tb *s, pcre *re, pcre_extra *extra)
{
        char const *r = RIGHT(s);

        int out[3];
        int rc = pcre_exec(re, extra, r, s->rightcount, 0, 0, out, 3);

        /* no match between the cursor and the end of the buffer */
        if (rc < 1)
                return false;

        /* there was a match. move to it. */
        count(r, out[0]);

        memcpy(s->left + s->leftcount, r, outpos.bytes);
        s->character += outpos.graphemes;
        s->line += outpos.lines;
        s->leftcount += outpos.bytes;
        s->rightcount -= outpos.bytes;
        s->column = outpos.column + ((outpos.lines >= 1) ? 0 : s->column);

        return true;
}

bool
tb_next_match_string(struct tb *s, char const *p, int bytes)
{
        char const *r = RIGHT(s);

        char const *m = strstrn(r, s->rightcount, p, bytes);

        /* no match between the cursor and the end of the buffer */
        if (m == NULL)
                return false;

        int n = m - r;

        /* there was a match. move to it. */
        count(r, n);

        memcpy(s->left + s->leftcount, r, outpos.bytes);
        s->character += outpos.graphemes;
        s->line += outpos.lines;
        s->leftcount += outpos.bytes;
        s->rightcount -= outpos.bytes;
        s->column = outpos.column + ((outpos.lines >= 1) ? 0 : s->column);

        return true;
}

static void
tb_pushs(struct tb *s, char const *data)
{
        s->character = s->characters;
        s->line = s->lines;
        pushn(s, data, strlen(data), false);
}

TEST(create)
{
        struct tb s = tb_new();

        claim(s.leftcount == 0);
        claim(s.rightcount == 0);

        claim(s.left != NULL);
        claim(s.right != NULL);

        claim(s.capacity == INITIAL_CAPACITY);
}

TEST(push)
{
        struct tb s = tb_new();
        tb_pushs(&s, "hello");

        claim(s.capacity == INITIAL_CAPACITY);
        claim(s.leftcount == 5);
        claim(s.rightcount == 0);

        claim(s.left != NULL);
        claim(s.right != NULL);
}

TEST(size)
{
        struct tb s = tb_new();

        tb_pushs(&s, "hello");
        tb_pushs(&s, " ");
        tb_pushs(&s, "world");

        claim(tb_size(&s) == 11);
}

TEST(remove)
{
        struct tb s = tb_new();

        tb_pushs(&s, "hello");
        tb_pushs(&s, " ");
        tb_pushs(&s, "world");

        tb_seek(&s, 5);
        tb_remove(&s, 1);
        claim(tb_compare_cstr(&s, "helloworld") == 0);

        tb_seek(&s, 0);
        tb_remove(&s, 11);
        claim(tb_size(&s) == 0);
}

TEST(width)
{
        struct tb s = tb_new();

        tb_pushs(&s, "ႠႡႢႣༀ༁⌛☠♊");
        claim(s.column == 9);
}

TEST(cstr)
{
        struct tb s = tb_new();
        tb_pushs(&s, "abc");
        tb_pushs(&s, " ");
        tb_pushs(&s, "foo");

        claim(strcmp(tb_cstr(&s), "abc foo") == 0);
}

TEST(cjk_remove)
{
        struct tb s = tb_new();
        tb_pushs(&s, "乔 乕 乖 乗 乘");
        claim(s.column == 14);

        tb_seek(&s, 0);
        tb_remove(&s, 2);
        claim(strcmp(tb_cstr(&s), "乕 乖 乗 乘") == 0);
}

TEST(forward)
{
        struct tb s = tb_new();

        tb_pushs(&s, "this 乔 乕 乖 乗 hello\n乘 world");

        tb_seek(&s, 0);

        claim(s.column == 0);
        claim(s.line == 0);
        claim(s.character == 0);

        claim(tb_forward(&s, 20) == 20);
        claim(s.column == 2);
        claim(s.line == 1);
}

TEST(backward)
{
        struct tb s = tb_new();

        tb_pushs(&s, "this 乔 乕 乖 乗 hello\n乘 world");
        claim(s.character == 26);
        claim(s.line == 1);
        claim(s.column == 8);

        claim(tb_backward(&s, 1) == 1);
        claim(s.character == 25);
        claim(s.column == 7);
        claim(s.line == 1);

        claim(tb_backward(&s, 4) == 4);
        claim(s.character == 21);
        claim(s.column == 3);
        claim(s.line == 1);

        claim(tb_backward(&s, 2) == 2);
        claim(s.character == 19);
        claim(s.column == 0);
        claim(s.line == 1);

        claim(tb_backward(&s, 1) == 1);
        claim(s.character == 18);
        claim(s.column == 22);
        claim(s.line == 0);
}

TEST(clone_line)
{
        struct tb s = tb_new();

        tb_pushs(&s, "this 乔 乕 乖 乗 hello\n乘 world");

        tb_seek(&s, 5);

        char const *line = tb_clone_line(&s);
        claim(strcmp(line, "this 乔 乕 乖 乗 hello") == 0);

        tb_seek(&s, 0);
        claim(strcmp(tb_clone_line(&s), "this 乔 乕 乖 乗 hello") == 0);
        tb_seek(&s, 18);
        claim(strcmp(tb_clone_line(&s), "this 乔 乕 乖 乗 hello") == 0);
}

TEST(up)
{
        struct tb s = tb_new();

        tb_pushs(&s, "this 乔 乕 乖 乗 hello\n乘 world");
        tb_seek(&s, 21);
        claim(s.line == 1);
        
        tb_up(&s, 1);
        claim(s.line == 0);
        claim(s.column == 3);
}

TEST(down)
{
        struct tb s = tb_new();

        tb_pushs(&s, "this 乔 乕 乖 乗 hello\n乘 world");
        tb_seek(&s, 0);
        claim(s.line == 0);

        claim(tb_down(&s, 1) == 1);
        claim(s.line == 1);
        claim(s.column == 0);
}

TEST(marker)
{
        struct tb s = tb_new();

        tb_pushs(&s, "TEST TEST");

        int *m1 = tb_new_marker(&s, 2);
        int *m2 = tb_new_marker(&s, 5);

        tb_seek(&s, 4);
        tb_insert(&s, "test ", 5);

        claim(*m1 == 2);
        claim(*m2 == 10);
}

TEST(history)
{
        struct tb s = tb_new();

        tb_pushs(&s, "TEST TEST\n");

        claim(tb_compare_cstr(&s, "TEST TEST\n") == 0);

        tb_start_new_edit(&s);
        claim(s.edits.count == 1);

        s.record_history = true;
        tb_append_line(&s, "HELLO", 5);
        claim(s.edits.count == 1);
        claim(CURRENT_EDIT(&s)->changes.count == 1);
        claim(strncmp(CURRENT_EDIT(&s)->changes.items[0].data.items, "HELLO\n", 6) == 0);
}

TEST(undo)
{
        struct tb s = tb_new();

        tb_pushs(&s, "TEST TEST\n");

        claim(tb_compare_cstr(&s, "TEST TEST\n") == 0);

        tb_start_new_edit(&s);
        claim(s.edits.count == 1);

        s.record_history = true;
        tb_append_line(&s, "HELLO", 5);
        claim(s.edits.count == 1);
        claim(CURRENT_EDIT(&s)->changes.count == 1);
        claim(strncmp(CURRENT_EDIT(&s)->changes.items[0].data.items, "HELLO\n", 6) == 0);

        claim(tb_compare_cstr(&s, "TEST TEST\nHELLO\n") == 0);

        claim(tb_undo(&s));

        claim(tb_compare_cstr(&s, "TEST TEST\n") == 0);
}

TEST(undo_grouping)
{
        struct tb s = tb_new();

        tb_insert(&s, "HELLO", 5);

        claim(tb_compare_cstr(&s, "HELLO") == 0);

        tb_start_history(&s);
        tb_start_new_edit(&s);
        claim(s.edits.count == 1);

        char const *data = " WORLD!";
        for (char const *c = data; *c != '\0'; ++c) {
                tb_insert(&s, c, 1);
        }

        claim(tb_compare_cstr(&s, "HELLO WORLD!") == 0);

        claim(s.character == 12);
        claim(tb_backward(&s, 1) == 1);
        claim(s.character == 11);

        claim(tb_remove(&s, 1) == 1);

        claim(tb_compare_cstr(&s, "HELLO WORLD") == 0);

        claim(tb_undo(&s));

        claim(tb_compare_cstr(&s, "HELLO") == 0);
}

TEST(find_next)
{
        struct tb s = tb_new();

        tb_pushs(&s, "this 乔 乕 乖 乗 hello\n乘 world");
        tb_seek(&s, 0);

        claim(tb_find_next(&s, "乔", strlen("乔")));
        claim(s.character == 5);

        claim(!tb_find_next(&s, "z", strlen("z")));
        claim(s.character == 5);
}


TEST(find_prev)
{
        struct tb s = tb_new();

        tb_pushs(&s, "this 乔 乕 乖 乗 hello\n乘 world");
        tb_seek(&s, 8);

        claim(tb_find_prev(&s, "s", 1));
        claim(s.character == 3);

        claim(!tb_find_prev(&s, "z", strlen("z")));
        claim(s.character == 3);
}
