#ifndef EDITOR_H_INCLUDED
#define EDITOR_H_INCLUDED

#include <stdint.h>
#include "vec.h"
#include "buffer.h"
#include "window.h"

struct editor {
        uintmax_t nextbufid;
        vec(struct buffer *) buffers;

        struct buffer *console;

        struct window *root_window;
        struct window *current_window;

        int status_timeout;
        char status[1024];
};

void
editor_init(struct editor *e, int lines, int cols);

unsigned
editor_create_file_buffer(struct editor *e, char const *path);

void
editor_view_buffer(struct editor *e, struct window *w, unsigned buf_id);

void
editor_destroy_buffer(struct editor *e, unsigned buf_id);

void
editor_destroy_all_buffers(struct editor *e);

void
editor_handle_text_input(struct editor *e, char const *s);

void
editor_handle_key_input(struct editor *e, char const *s);

void
editor_do_update(struct editor *e);

#endif
