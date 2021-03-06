#ifndef BUFFER_H_INCLUDED
#define BUFFER_H_INCLUDED

#include <stdint.h>
#include <stdbool.h>
#include <setjmp.h>
#include <stdarg.h>

#include <unistd.h>
#include <pthread.h>
#include <pcre.h>

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

void
buffer_window_left(void);

void
buffer_window_right(void);

void
buffer_window_up(void);

void
buffer_window_down(void);

void
buffer_goto_window(int id);

int
buffer_prev_line(int amount);

int
buffer_next_line(int amount);

int
buffer_scroll_y(void);

int
buffer_scroll_x(void);

int
buffer_scroll_up(int amount);

int
buffer_scroll_down(int amount);

int
buffer_right(int n);

int
buffer_left(int n);

void
buffer_end_of_line(void);

void
buffer_start_of_line(void);

void
buffer_start(void);

void
buffer_end(void);

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

struct value
buffer_next_char(int i);

struct value
buffer_get_char(void);

struct value
buffer_get_line(int i);

void
buffer_each_line(struct value *f);

struct value
buffer_save_excursion(struct value *f);

int
buffer_point(void);

void
buffer_log(char const *s);

void
buffer_echo(char const *s, int n);

bool
buffer_undo(void);

bool
buffer_redo(void);

void
buffer_mark_values(void);

void
buffer_center_current_line(void);

int
buffer_seek(int i);

bool
buffer_next_match_regex(pcre *re, pcre_extra *extra);

bool
buffer_next_match_string(char const *s, int n);

int
buffer_spawn(char *path, struct value_array *args, struct value on_output, struct value on_exit);

bool
buffer_kill_subprocess(int fd);

bool
buffer_close_subprocess(int fd);

bool
buffer_wait_subprocess(int fd);

bool
buffer_write_to_subprocess(int fd, char const *data, int n);

void
buffer_save_file(void);

void
buffer_write_file(char const *path, int n);

void
buffer_load_file(char const *path, int n);

void
buffer_clear(void);

void
buffer_clear_to_start(void);

void
buffer_clear_to_end(void);

char const *
buffer_file_name(void);

void
buffer_show_console(void);

int
buffer_horizontal_split(int, int);

int
buffer_vertical_split(int, int);

struct value
buffer_window_height(void);

struct value
buffer_window_width(void);

int
buffer_window_id(void);

void
buffer_delete_window(void);

void
buffer_register_message_handler(struct value type, struct value f);

void
buffer_send_message(int id, char const *type, int tn, char const *msg, int mn);

int
buffer_id(void);

int
buffer_create(char const *prog, int n);

void
buffer_cycle_window_color(void);

bool
buffer_write_to_proc(int);

void
buffer_source(void);

void
buffer_goto_line(int);

bool
buffer_find_forward(char const *s, int n);

bool
buffer_find_backward(char const *s, int n);

void
blog(char const *fmt, ...);

#endif
