#ifndef BUFFER_H_INCLUDED
#define BUFFER_H_INCLUDED

#include <stdint.h>
#include <stdbool.h>
#include <setjmp.h>

#include <unistd.h>
#include <pthread.h>

#include "file.h"
#include "textbuffer.h"
#include "value.h"

jmp_buf buffer_err_jb;

struct buffer {

        /*
         * true  - buffer process is using rb1 and parent should read from rb2
         * false - buffer process is using rb2 and parent should read from rb1
         */
        bool *rb_idx;
        bool *rb_changed;
        pthread_mutex_t *rb_mtx; // can't read/write rb_idx unless this is locked
        char *rb1;
        char *rb2;

        unsigned id;
        pid_t pid;

        int write_fd;
        int read_fd;

        /*
         * null pointer if the buffer is not associated with a window.
         */
        struct window *window;
};

/*
 * This is called by the main editor process to spawn children.
 */
struct buffer
buffer_new(unsigned id);


/*
 * These functions are called within the child process and should never be called by the parent.
 */
void
buffer_insert_n(char const *text, int n);

int
buffer_remove(int n);

char *
buffer_current_line(void);

char *
buffer_get_line(int n);

int
buffer_forward(int n);

int
buffer_backward(int n);

int
buffer_lines(void);

int
buffer_line(void);

int
buffer_column(void);

void
buffer_grow_y(int amount);

void
buffer_grow_x(int amount);

void
buffer_next_window(void);

void
buffer_prev_window(void);

int
buffer_prev_line(int amount);

int
buffer_next_line(int amount);

int
buffer_scroll_up(int amount);

int
buffer_scroll_down(int amount);

int
buffer_right(int n);

int
buffer_left(int n);

void
buffer_map_normal(struct value_array *chord, struct value action);

void
buffer_map_insert(struct value_array *chord, struct value action);

void
buffer_normal_mode(void);

void
buffer_insert_mode(void);

void
buffer_cut_line(void);

void
buffer_mark_values(void);

#endif
