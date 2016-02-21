#include <assert.h>
#include <signal.h>

#include "editor.h"
#include "buffer.h"
#include "vec.h"
#include "protocol.h"
#include "window.h"

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

struct editor
editor_new(int lines, int cols)
{
        struct editor e;

        e.nextbufid = 1;
        e.root_window = window_new(NULL, 0, 0, cols, lines);
        vec_init(e.buffers);

        return e;
}

unsigned
editor_create_file_buffer(struct editor *e, char const *path)
{
        struct buffer *b = vec_push(e->buffers, buffer_new(e->nextbufid++));
        int bytes = strlen(path);

        evt_send(b->write_fd, EVT_LOAD_FILE);
        sendint(b->write_fd, bytes);
        write(b->write_fd, path, bytes);

        return b->id;
}

void
editor_view_buffer(struct editor *e, struct window *w, unsigned buf_id)
{
        struct buffer *b = findbuffer(e, buf_id);
        assert(b != NULL);

        assert(w->type == WINDOW_WINDOW);
        w->buffer = b;

        evt_send(b->write_fd, EVT_WINDOW_DIMENSIONS);
        sendint(b->write_fd, w->height);
        sendint(b->write_fd, w->width);
}

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

void
editor_handle_text_input(struct editor *e, char const *s)
{
        struct buffer *b = e->current_window->buffer;
        assert(b != NULL);

        int bytes = strlen(s);

        evt_send(b->write_fd, EVT_TEXT_INPUT);
        sendint(b->write_fd, bytes);
        write(b->write_fd, s, bytes);
}
