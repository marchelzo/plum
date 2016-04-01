#include <assert.h>
#include <signal.h>

#include <ncurses.h>

#include "editor.h"
#include "buffer.h"
#include "vec.h"
#include "protocol.h"
#include "window.h"
#include "log.h"

inline static void
deletewindow(struct editor *e, struct window *w)
{
        if (e->current_window->parent == w->parent)
                e->current_window = w->parent;

        window_delete(w);
}

inline static struct buffer *
newbuffer(struct editor *e)
{
        return vec_push(e->buffers, buffer_new(e->nextbufid++));
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
 * Handle an event received from a buffer.
 */
static void
handle_event(struct editor *e, buffer_event_code c, struct buffer *b)
{
        static char buf[1024];
        int bytes;
        int amount;

        switch (c) {
        case EVT_GROW_X_REQUEST:
                amount = recvint(b->read_fd);
                window_grow_x(b->window, amount);
                break;
        case EVT_GROW_Y_REQUEST:
                amount = recvint(b->read_fd);
                window_grow_y(b->window, amount);
                break;
        case EVT_NEXT_WINDOW_REQUEST:
                if (b->window == e->current_window) {
                        e->current_window = window_next(b->window);
                }
                break;
        case EVT_PREV_WINDOW_REQUEST:
                if (b->window == e->current_window) {
                        e->current_window = window_prev(b->window);
                }
        case EVT_HSPLIT_REQUEST: // TODO
        case EVT_VSPLIT_REQUEST: // TODO
                break;
        case EVT_SHOW_CONSOLE_REQUEST:
                showconsole(e);
                break;
        case EVT_VM_ERROR:
                beep();
        case EVT_LOG_REQUEST:
                bytes = recvint(b->read_fd);
                read(b->read_fd, buf, bytes);
                evt_send(e->console->write_fd, EVT_LOG_REQUEST);
                sendint(e->console->write_fd, bytes);
                write(e->console->write_fd, buf, bytes);
                break;
        }
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
                struct buffer *b = vec_get(e->buffers, mid);
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
 * Check the pipe of each buffer process to see if any of them have
 * sent any data to us.
 */
static void
handle_events(struct editor *e)
{
        int n = vec_len(e->buffers);

        buffer_event_code c;
        for (int i = 0; i < n; ++i) {
                if (read(vec_get(e->buffers, i)->read_fd, &c, sizeof c) == 1) {
                        handle_event(e, c, vec_get(e->buffers, i));
                } else if (errno != EWOULDBLOCK) {
                        panic("read() failed: %s", strerror(errno));
                }
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

/*
 * Create a new editor with one root window and no open buffers.
 */
void
editor_init(struct editor *e, int lines, int cols)
{
        e->nextbufid = 0;
        vec_init(e->buffers);

        e->root_window = window_new(NULL, 0, 0, cols, lines);
        e->current_window = e->root_window;

        e->console = newbuffer(e);
}

/*
 * Open a new buffer in the editor pointed to be 'e', with the contents
 * of the file whose path is 'path'.
 *
 * Returns the buffer id of the new buffer.
 */
unsigned
editor_create_file_buffer(struct editor *e, char const *path)
{
        struct buffer *b = newbuffer(e);
        int bytes = strlen(path);

        evt_send(b->write_fd, EVT_LOAD_FILE);
        sendint(b->write_fd, bytes);
        write(b->write_fd, path, bytes);

        return b->id;
}

/*
 * Make the buffer with id 'buf_id' be the buffer associated with the window
 * pointed to by 'w'. If there was previously another buffer associated with 'w',
 * it will no longer be assoicated with a window.
 */
void
editor_view_buffer(struct editor *e, struct window *w, unsigned buf_id)
{
        struct buffer *b = findbuffer(e, buf_id);
        assert(b != NULL);

        assert(w->type == WINDOW_WINDOW);
        w->buffer = b;
        b->window = w;

        evt_send(b->write_fd, EVT_WINDOW_DIMENSIONS);
        sendint(b->write_fd, w->height - 1);
        sendint(b->write_fd, w->width);
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
        struct buffer *b;
        vec_for_each(e->buffers, _, b) {
                kill(b->pid, SIGTERM);
        }
}

/*
 * Causes the editor pointed to by 'e' to receive and process the key event
 * described by the string 's' (s is returned to us by libtickit).
 */
void
editor_handle_key_input(struct editor *e, char const *s)
{
        struct buffer *b = current_buffer(e);
        assert(b != NULL);

        int bytes = strlen(s);

        evt_send(b->write_fd, EVT_KEY_INPUT);
        sendint(b->write_fd, bytes);
        write(b->write_fd, s, bytes);
}

/*
 * Causes the editor poitned to by 'e' to receive and process the text 's'.
 */
void
editor_handle_text_input(struct editor *e, char const *s)
{
        struct buffer *b = current_buffer(e);
        assert(b != NULL);

        int bytes = strlen(s);

        LOG("s = <%s>", s);
        LOG("bytes = %d", bytes);

        /*
         * Send the text to the child process associated with the current buffer.
         */
        evt_send(b->write_fd, EVT_TEXT_INPUT);
        sendint(b->write_fd, bytes);
        write(b->write_fd, s, bytes);
}

void
editor_do_update(struct editor *e)
{
        handle_events(e);
}
