#ifndef EDITOR_H_INCLUDED
#define EDITOR_H_INCLUDED

#include <stdint.h>
#include <stdbool.h>

#include <poll.h>

#include "vec.h"
#include "buffer.h"
#include "window.h"

struct editor {
        int nbufs;
        vec(struct buffer *) buffers;

        struct buffer *console;

        struct window *root_window;
        struct window *current_window;

        vec(struct pollfd) pollfds;

        bool background;
};

void
editor_init(struct editor *e, int lines, int cols);

void
editor_destroy_all_buffers(struct editor *e);

void
editor_handle_input(struct editor *e, char const *s);

void
editor_foreground(struct editor *e);

void
editor_background(struct editor *e);

void
editor_run(struct editor *e);

#endif
