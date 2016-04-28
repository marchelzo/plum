#ifndef WINDOW_H_INCLUDED
#define WINDOW_H_INCLUDED

#include <stdbool.h>

#include <ncurses.h>

#include "window.h"

#define WINDOW_SIBLING(w) (((w)->parent->one == (w)) ? (w)->parent->two : (w)->parent->one)

struct window {
        enum { WINDOW_VSPLIT, WINDOW_HSPLIT, WINDOW_WINDOW } type;
        int x, y;    // top-left coords
        int height;  // height in rows
        int width;   // width in columns
        bool redraw;
        bool insert_mode;
        int color;
        struct window *parent;
        union {
                struct {
                        struct window *left;
                        struct window *right;
                };
                struct {
                        struct window *top;
                        struct window *bot;
                };
                struct {
                        struct window *one;
                        struct window *two;
                };
                struct {
                        WINDOW *window;
                        struct buffer *buffer;
                        struct { int x, y; } cursor;
                        unsigned id;
                };
        };
};

struct window *
window_root(int x, int y, int width, int height);

void
window_notify_dimensions(struct window const *w);

struct window *
window_find_leaf(struct window *w);

struct window *
window_next(struct window *);

struct window *
window_prev(struct window *);

struct window *
window_right(struct window *);

struct window *
window_left(struct window *);

struct window *
window_up(struct window *);

struct window *
window_down(struct window *);

void
window_grow_y(struct window *w, int dy);

void
window_grow_x(struct window *w, int dx);

void
window_set_height(struct window *w, int height);

void
window_set_width(struct window *w, int width);

void
window_vsplit(struct window *w, struct buffer *b);

void
window_hsplit(struct window *w, struct buffer *b);

void
window_delete(struct window *w);

struct window *
window_search(struct window *w, int id);

void
window_touch(struct window *w);

void
window_resize(struct window *w, int height, int width);

void
window_cycle_color(struct window *w);

#endif
