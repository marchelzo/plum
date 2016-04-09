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
#include "location.h"
#include "test.h"
#include "util.h"
#include "panic.h"
#include "file.h"
#include "buffer.h"
#include "protocol.h"
#include "window.h"
#include "log.h"

inline static char *
getdata(struct buffer *b)
{
        return *b->rb_idx ? b->rb2 : b->rb1;
}

static bool
render_window(struct window *w)
{
        int lines;
        int bytes;
        struct buffer *b = w->buffer;
        bool changed = false;

        pthread_mutex_lock(b->rb_mtx);

        if (!*b->rb_changed && !w->force_redraw)
                goto done;
        else
                changed = true;

        werase(w->window);

        char const *src = getdata(b);

        src = readint(src, &bytes);

        wattron(w->window, A_BOLD);
        wbkgdset(w->window, A_REVERSE);
        mvwaddnstr(w->window, w->height - 2, 0, src, bytes);
        wattroff(w->window, A_BOLD);
        wbkgdset(w->window, A_NORMAL);

        src += bytes;

        src = readint(src, &w->cursor.y);
        src = readint(src, &w->cursor.x);
        w->insert_mode = *src++;

        src = readint(src, &lines);
        for (int line = 0; line < lines; ++line) {
                src = readint(src, &bytes);
                mvwaddnstr(w->window, line, 0, src, bytes);
                src += bytes;
        }

        wnoutrefresh(w->window);

        *b->rb_changed = false;

done:
        pthread_mutex_unlock(b->rb_mtx);
        return changed;
}

static bool
draw(struct window *w)
{
        bool changed = false;

        switch (w->type) {
        case WINDOW_VSPLIT:
                changed |= draw(w->top);
                changed |= draw(w->bot);
                mvhline(w->bot->y - 1, w->x, ACS_HLINE, w->width);
                break;
        case WINDOW_HSPLIT:
                changed |= draw(w->left);
                changed |= draw(w->right);
                mvvline(w->y, w->right->x - 1, ACS_VLINE, w->height);
                break;
        case WINDOW_WINDOW: 
                changed = render_window(w);
                break;
        }

        w->force_redraw = false;

        return changed;
}

void
render(struct editor *e)
{
        static bool insert_mode = false;

        if (!draw(e->root_window))
                return;
        
        int y = e->current_window->y + e->current_window->cursor.y;
        int x = e->current_window->x + e->current_window->cursor.x;
        move(y, x);

        if (insert_mode != e->current_window->insert_mode) {
                if (e->current_window->insert_mode)
                        write(1, INSERT_BEGIN_STRING, sizeof INSERT_BEGIN_STRING - 1);
                else
                        write(1, INSERT_END_STRING, sizeof INSERT_END_STRING - 1);
        }

        insert_mode = e->current_window->insert_mode;

        wnoutrefresh(stdscr);
        doupdate();
}
