#ifndef WINDOW_H_INCLUDED
#define WINDOW_H_INCLUDED

#include <stdbool.h>

#include <ncurses.h>

#include "window.h"

struct window {
        enum { WINDOW_VSPLIT, WINDOW_HSPLIT, WINDOW_WINDOW } type;
        int x, y;    // top-left coords
        int height;  // height in rows
        int width;   // width in columns
        bool force_redraw;
        bool insert_mode;
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
                        unsigned id;
                        struct buffer *buffer;
                        struct { int x, y; } cursor;
                };
        };
};

struct window *
window_new(
        struct window *parent,
        int x,
        int y,
        int width,
        int height
);

struct window *
window_next(struct window *);

struct window *
window_prev(struct window *);

void
window_grow_y(struct window *w, int dy);

void
window_grow_x(struct window *w, int dx);

void
window_vsplit(struct window *w);

void
window_hsplit(struct window *w);

void
window_delete(struct window *w);

#endif
