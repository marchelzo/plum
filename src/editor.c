#include <assert.h>
#include <signal.h>
#include <unistd.h>

#include <ncurses.h>
#include <poll.h>

#include "editor.h"
#include "buffer.h"
#include "vec.h"
#include "protocol.h"
#include "window.h"
#include "config.h"
#include "render.h"
#include "term.h"
#include "log.h"

inline static void
deletewindow(struct editor *e, struct window *w)
{
        bool move = e->current_window->parent == w->parent;
        if (move)
                e->current_window = w->parent;

        window_delete(w);

        if (move)
                e->current_window = window_find_leaf(e->current_window);
}

inline static struct buffer *
newbuffer(struct editor *e)
{
        struct buffer *b = alloc(sizeof *b);

        *b = buffer_new(e->nbufs++);
        vec_push(e->buffers, b);

        vec_push(e->pollfds, ((struct pollfd){ .fd = b->read_fd, .events = POLLIN }));

        return b;
}

inline static void
showconsole(struct editor *e)
{
        if (e->console->window != NULL)
                deletewindow(e, e->console->window);

        struct window *w = e->current_window;
        window_vsplit(w, e->console);
        e->current_window = w->top;
}

/*
 * Binary search for a buffer given a buffer id.
 */
static struct buffer *
findbuffer(struct editor *e, unsigned id)
{
        int lo = 0;
        int hi = vec_len(e->buffers) - 1;

        while (lo <= hi) {
                int mid = lo/2 + hi/2 + (hi & lo & 1);
                struct buffer *b = *vec_get(e->buffers, mid);
                if (b->id > id) {
                        hi = mid - 1;
                } else if (b->id < id) {
                        lo = mid + 1;
                } else {
                        return b;
                }
        }

        return NULL;
}

/*
 * Binary search for a buffer given a buffer fd.
 */
static struct buffer *
findbyfd(struct editor *e, int fd)
{
        int lo = 0;
        int hi = vec_len(e->buffers) - 1;

        while (lo <= hi) {
                int mid = lo/2 + hi/2 + (hi & lo & 1);
                struct buffer *b = *vec_get(e->buffers, mid);
                if (b->read_fd > fd) {
                        hi = mid - 1;
                } else if (b->read_fd < fd) {
                        lo = mid + 1;
                } else {
                        return b;
                }
        }

        return NULL;
}

/*
 * Handle an event received from a buffer.
 */
static void
handle_event(struct editor *e, buffer_event_code c, struct buffer *b)
{
        static struct window *window;
        static struct buffer *buffer;
        static char buf[4096];
        static char msgbuf[256];
        static int bytes, msgbytes;
        static int amount;
        static int id;
        static int size;

        void (*split)(struct window *, struct buffer *) = NULL;

        switch (c) {
        case EVT_RENDER:
                render(e);
                break;
        case EVT_GROW_X:
                amount = recvint(b->read_fd);
                window_grow_x(b->window, amount);
                break;
        case EVT_GROW_Y:
                amount = recvint(b->read_fd);
                window_grow_y(b->window, amount);
                break;
        case EVT_WINDOW_RIGHT:
        case EVT_WINDOW_LEFT:
        case EVT_WINDOW_DOWN:
        case EVT_WINDOW_UP:
        case EVT_PREV_WINDOW:
        case EVT_NEXT_WINDOW:

                if (b->window != e->current_window)
                        break;

                switch (c) {
                case EVT_WINDOW_RIGHT: window = window_right(e->current_window); break;
                case EVT_WINDOW_LEFT:  window = window_left(e->current_window);  break;
                case EVT_WINDOW_DOWN:  window = window_down(e->current_window);  break;
                case EVT_WINDOW_UP:    window = window_up(e->current_window);    break;
                case EVT_NEXT_WINDOW:  window = window_next(e->current_window);  break;
                case EVT_PREV_WINDOW:  window = window_prev(e->current_window);  break;
                }

                if (window == NULL)
                        break;

                e->current_window = window;
                window->redraw = true;

                break;
        case EVT_GOTO_WINDOW:
                id = recvint(b->read_fd);
                if (b->window == e->current_window) {
                        struct window *w = window_search(e->root_window, id);
                        if (w != NULL)
                                e->current_window = w;
                }
                break;
        case EVT_WINDOW_CYCLE_COLOR:
                if (b->window != NULL)
                        window_cycle_color(b->window);
                break;
        case EVT_HSPLIT:
                split = window_hsplit;
        case EVT_VSPLIT:
                if (split == NULL)
                        split = window_vsplit;
                if (b->window != NULL) {
                        id = recvint(b->read_fd);
                        size = recvint(b->read_fd);
                        buffer = (id == -1) ? newbuffer(e) : findbuffer(e, id);

                        if (buffer == NULL) {
                                evt_send(b->write_fd, EVT_WINDOW_ID);
                                sendint(b->write_fd, -1);
                        } else {
                                split(b->window, buffer);
                                e->current_window = b->window;
                                evt_send(b->write_fd, EVT_WINDOW_ID);
                                sendint(b->write_fd, WINDOW_SIBLING(b->window)->id);

                                if (size == -1)
                                        break;

                                if (split == window_vsplit)
                                        window_set_height(WINDOW_SIBLING(b->window), size);
                                else if (split == window_hsplit)
                                        window_set_width(WINDOW_SIBLING(b->window), size);
                        }
                }
                break;
        case EVT_WINDOW_ID:
                evt_send(b->write_fd, EVT_WINDOW_ID);
                if (b->window == NULL)
                        sendint(b->write_fd, -1);
                else
                        sendint(b->write_fd, b->window->id);
                break;
        case EVT_WINDOW_DELETE:
                deletewindow(e, b->window);
                break;
        case EVT_STATUS_MESSAGE:
                bytes = recvint(b->read_fd);
                /* TODO: do something reasonable if bytes is big */
                read(b->read_fd, buf, bytes);
                attron(A_BOLD);
                mvaddnstr(0, 0, buf, bytes);
                clrtoeol();
                attroff(A_BOLD);
                wnoutrefresh(stdscr);
                window_touch(e->root_window);
                break;
        case EVT_SHOW_CONSOLE:
                showconsole(e);
                break;
        case EVT_MESSAGE:
                id = recvint(b->read_fd);
                msgbytes = recvint(b->read_fd);
                read(b->read_fd, msgbuf, msgbytes);
                bytes = recvint(b->read_fd);
                if (bytes != -1)
                        read(b->read_fd, buf, bytes);
                buffer = findbuffer(e, id);
                if (buffer == NULL) {
                        evt_send(e->console->write_fd, EVT_LOG);
                        bytes = sprintf(buf, "Invalid buffer ID used as message target: %d", id);
                        sendint(e->console->write_fd, bytes);
                        write(e->console->write_fd, buf, bytes);
                } else {
                        evt_send(buffer->write_fd, EVT_MESSAGE);

                        sendint(buffer->write_fd, b->id);

                        sendint(buffer->write_fd, msgbytes);
                        write(buffer->write_fd, msgbuf, msgbytes);

                        sendint(buffer->write_fd, bytes);
                        if (bytes != -1)
                                write(buffer->write_fd, buf, bytes);
                }
                break;
        case EVT_NEW_BUFFER:
                buffer = newbuffer(e);

                evt_send(b->write_fd, EVT_NEW_BUFFER);
                sendint(b->write_fd, buffer->id);

                bytes = recvint(b->read_fd);
                if (bytes == -1)
                        break;
                else
                        read(b->read_fd, buf, bytes);

                evt_send(buffer->write_fd, EVT_RUN_PROGRAM);
                sendint(buffer->write_fd, bytes);
                write(buffer->write_fd, buf, bytes);
                break;
        case EVT_VM_ERROR:
                beep();
        case EVT_LOG:
                bytes = recvint(b->read_fd);
                read(b->read_fd, buf, bytes);
                evt_send(e->console->write_fd, EVT_LOG);
                sendint(e->console->write_fd, bytes);
                write(e->console->write_fd, buf, bytes);
                break;
        }
}

/*
 * Get a pointer to the editor's current buffer.
 */
inline static struct buffer *
current_buffer(struct editor const *e)
{
        assert(e->current_window != NULL);
        return e->current_window->buffer;
}

void
editor_init(struct editor *e, int lines, int cols)
{
        e->nbufs = 0;
        vec_init(e->buffers);

        vec_init(e->pollfds);
        vec_push(e->pollfds, ((struct pollfd){ .fd = 0, .events = POLLIN }));

        e->root_window = window_root(0, 1, cols, lines - 1);
        e->current_window = e->root_window;

        e->console = newbuffer(e);

        struct buffer *b = newbuffer(e);
        b->window = e->current_window;
        e->current_window->buffer = b;
        window_notify_dimensions(e->current_window);

        e->background = false;
}

/*
 * Kill the process running the buffer whose id is 'buf_id'.
 */
void
editor_destroy_buffer(struct editor *e, unsigned buf_id)
{
        struct buffer *b = findbuffer(e, buf_id);
        assert(b != NULL);

        kill(b->pid, SIGTERM);
}

void
editor_destroy_all_buffers(struct editor *e)
{
        struct buffer **b;
        vec_for_each(e->buffers, _, b) {
                kill(b[0]->pid, SIGTERM);
        }
}

void
editor_handle_input(struct editor *e, char const *s)
{
        struct buffer *b = current_buffer(e);
        assert(b != NULL);

        int bytes = strlen(s);

        evt_send(b->write_fd, EVT_INPUT);
        sendint(b->write_fd, bytes);
        write(b->write_fd, s, bytes);
}

/*
 * Check the pipe of each buffer process to see if any of them have
 * sent any data to us.
 */
inline static void
update(struct editor *e)
{
        int const n = e->pollfds.count;

        poll(e->pollfds.items, n, -1);

        if (e->pollfds.items[0].revents & POLLIN)
                term_handle_input();

        for (int i = 1; i < n; ++i) {
                if (e->pollfds.items[i].revents & POLLIN) {
                        int fd = e->pollfds.items[i].fd;
                        handle_event(e, evt_recv(fd), findbyfd(e, fd));
                }
        }
}

void
editor_run(struct editor *e)
{
        for (;;) update(e);
}

void
editor_background(struct editor *e)
{
        e->background = true;
        e->pollfds.items[0].events &= ~POLLIN;
        e->pollfds.items[0].revents = 0;
}

void
editor_foreground(struct editor *e)
{
        e->background = false;
        e->pollfds.items[0].events |= POLLIN;
        window_touch(e->root_window);
        render(e);
}
