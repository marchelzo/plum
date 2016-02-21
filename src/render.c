#include <stdio.h>
#include <stdbool.h>
#include <errno.h>
#include <string.h>
#include <stdnoreturn.h>
#include <assert.h>
#include <tickit.h>
#include <ncurses.h>

#include <sys/stat.h>
#include <sys/mman.h>
#include <unistd.h>
#include <fcntl.h>

#include "editor.h"
#include "alloc.h"
#include "textbuffer.h"
#include "location.h"
#include "test.h"
#include "util.h"
#include "panic.h"
#include "file.h"
#include "buffer.h"
#include "protocol.h"
#include "window.h"
#include "log.h"

/*
 *
 */
inline static char *
getdata(struct buffer *b)
{
        return *b->rb_idx ? b->rb2 : b->rb1;
}

inline static bool
was_updated(struct buffer *b)
{
        pthread_mutex_lock(b->rb_mtx);
        bool changed = *b->rb_changed;
        pthread_mutex_unlock(b->rb_mtx);

        return changed;
}

static void
render_window(struct window *w)
{
        int lines;
        int bytes;
        struct buffer *b = w->buffer;

        pthread_mutex_lock(b->rb_mtx);

        char const *src = getdata(b);

        src = readint(src, &w->cursor.y);
        src = readint(src, &w->cursor.x);

        src = readint(src, &lines);
        for (int line = 0; line < lines; ++line) {
                src = readint(src, &bytes);
                mvaddnstr(line + w->y, w->x, src, bytes);
                src += bytes;
        }

        *b->rb_changed = false;

        pthread_mutex_unlock(b->rb_mtx);
}

static void
draw(struct window *w)
{
        switch (w->type) {
        case WINDOW_VSPLIT:
                draw(w->top);
                draw(w->bot);
                mvhline(w->bot->y - 1, w->x, ACS_HLINE, w->width);
                break;
        case WINDOW_HSPLIT:
                draw(w->left);
                draw(w->right);
                mvvline(w->y, w->right->x - 1, ACS_VLINE, w->height);
                break;
        case WINDOW_WINDOW: 
                render_window(w);
                break;
        }

        w->force_redraw = false;
}

static bool
need_redraw(struct window *w)
{
        if (w->force_redraw) {
                return true;
        }

        if (w->type == WINDOW_WINDOW) {
                return was_updated(w->buffer);
        } else {
                return need_redraw(w->one) || need_redraw(w->two);
        }
}

bool
render(struct editor *e)
{
        if (need_redraw(e->root_window)) {
                LOG("doing a redraw...");
                erase();
                draw(e->root_window);
                move(e->current_window->cursor.y, e->current_window->cursor.x);
                refresh();
                return true;
        }

        return false;
}
