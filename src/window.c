#include <assert.h>

#include "protocol.h"
#include "window.h"
#include "alloc.h"
#include "test.h"
#include "buffer.h"
#include "log.h"
#include "colors.h"

static int winid = 0;

inline static struct window *
root(struct window *w)
{
        while (w->parent != NULL)
                w = w->parent;

        return w;
}

static struct window *
find(struct window *w, int x, int y)
{
        switch (w->type) {
        case WINDOW_HSPLIT:
                if (x < w->left->x + w->left->width)
                        return find(w->left, x, y);
                else
                        return find(w->right, x, y);
        case WINDOW_VSPLIT:
                if (y < w->top->y + w->top->height)
                        return find(w->top, x, y);
                else
                        return find(w->bot, x, y);
        case WINDOW_WINDOW:
                return w;
        }
}

inline static void
refreshdimensions(struct window *w)
{
        switch (w->type) {
        case WINDOW_WINDOW:
                if (w->buffer == NULL)
                        return;
                w->buffer->window = w;
                window_notify_dimensions(w);
                break;
        case WINDOW_HSPLIT:
        case WINDOW_VSPLIT:
                refreshdimensions(w->one);
                refreshdimensions(w->two);
                break;

        }

	w->force_redraw = true;
}

inline static void
reconstruct_curses_window(struct window *w)
{
        delwin(w->window);
        w->window = newwin(w->height, w->width, w->y, w->x);
        wbkgdset(w->window, COLOR_PAIR(w->color));
}

static void
fixtree(struct window *w)
{
        int extra;
        switch (w->type) {
        case WINDOW_VSPLIT:
                w->one->width = w->width;
                w->two->width = w->width;

                extra = w->height - (w->one->height + w->two->height);
                w->one->height += (extra / 2) + (extra & 1);
                w->two->height += (extra / 2);

                w->one->y = w->y;
                w->two->y = w->y + w->one->height;
                break;
        case WINDOW_HSPLIT:
                w->one->height = w->height;
                w->two->height = w->height;

                extra = w->width - (w->one->width + w->two->width);
                w->one->width += (extra / 2) + (extra & 1);
                w->two->width += (extra / 2);

                w->one->x = w->x;
                w->two->x = w->x + w->one->width;
                break;
        case WINDOW_WINDOW:
                reconstruct_curses_window(w);
                return;
        }

        fixtree(w->one);
        fixtree(w->two);
}

static void
propagate(struct window *w, int dx, int dy, int dw, int dh)
{
        w->x += dx;
        w->y += dy;

        w->width += dw;
        w->height += dh;

        w->force_redraw = true;

        switch (w->type) {
        case WINDOW_VSPLIT:
                propagate(w->top, dx, dy, dw, dh);
                propagate(w->bot, dx, dy, dw, dh);
                return;
        case WINDOW_HSPLIT:
                propagate(w->left, dx, dy, dw, dh);
                propagate(w->right, dx, dy, dw, dh);
                return;
        case WINDOW_WINDOW:
                refreshdimensions(w);
                reconstruct_curses_window(w);
                return;
        }
}

inline static void
delete(struct window *w)
{
        colors_free(w->color);

        delwin(w->window);

        w->buffer->window = NULL;
        evt_send(w->buffer->write_fd, EVT_BACKGROUNDED);

        free(w);
}

static struct window *
window_new(
        struct window *parent,
        int x,
        int y,
        int width,
        int height,
        int color
) {
        struct window *w = alloc(sizeof *w);

        w->type = WINDOW_WINDOW;

        w->color = color;
        w->window = newwin(height, width, y, x);
        wbkgdset(w->window, COLOR_PAIR(color));

        w->buffer = NULL;
        w->id = winid++;

        w->parent = parent;
        w->x = x;
        w->y = y;
        w->width = width;
        w->height = height;
        w->force_redraw = true;
        w->insert_mode = false;

        return w;
}

struct window *
window_root(int x, int y, int width, int height)
{
        return window_new(NULL, x, y, width, height, colors_next(-1));
}

void
window_notify_dimensions(struct window const *w)
{
        evt_send(w->buffer->write_fd, EVT_WINDOW_DIMENSIONS);
        sendint(w->buffer->write_fd, w->height);
        sendint(w->buffer->write_fd, w->width);
}

struct window *
window_find_leaf(struct window *w)
{
        while (w->type != WINDOW_WINDOW)
                w = w->one;

        return w;
}

struct window *
window_next(struct window *w)
{
        assert(w->type == WINDOW_WINDOW);

        struct window *p = w->parent;

        if (p == NULL) {
                return w;
        }

        while (p != NULL && (p->two == w || p->two == NULL)) {
                w = p;
                p = p->parent;
        }

        w = (p == NULL) ? w : p->two;

        while (w->type != WINDOW_WINDOW)
                w = w->one;

        return w;
}

struct window *
window_prev(struct window *w)
{
        struct window *p = w->parent;

        if (p == NULL) {
                return w;
        }

        while (p != NULL && (p->one == w || p->one == NULL)) {
                w = p;
                p = p->parent;
        }

        w = (p == NULL) ? w : p->one;

        while (w->type != WINDOW_WINDOW)
                w = w->two;

        return w;
}

struct window *
window_right(struct window *w)
{
        struct window *r = root(w);
        if (w->x + w->width == r->width)
                return w;
        else
                return find(r, w->x + w->width, w->y);
}

struct window *
window_left(struct window *w)
{
        struct window *r = root(w);
        if (w->x == 0)
                return w;
        else
                return find(r, w->x - 1, w->y);
}

struct window *
window_up(struct window *w)
{
        struct window *r = root(w);
        if (w->y == 0)
                return w;
        else
                return find(r, w->x, w->y - 1);
}

struct window *
window_down(struct window *w)
{
        struct window *r = root(w);
        if (w->y + w->height == r->height)
                return w;
        else
                return find(r, w->x, w->y + w->height);
}

void
window_grow_y(struct window *w, int dy)
{
        assert(w != NULL);
        assert(w->parent != NULL);

        struct window *p = w->parent;

        if (p->type != WINDOW_VSPLIT) {
                window_grow_y(p, dy);
                return;
        }

        if (w == p->top) {
                // we are on top, so we don't need to translate
                // our side of the .
                propagate(w, 0, 0, 0, dy);
                propagate(p->bot, 0, dy, 0, -dy);
        } else {
                // here we _do_ need to translate our side
                propagate(w, 0, -dy, 0, dy);
                propagate(p->top, 0, 0, 0, -dy);
        }
}

void
window_grow_x(struct window *w, int dx)
{
        assert(w != NULL);
        assert(w->parent != NULL); // can't grow the root window

        struct window *p = w->parent;

        if (p->type != WINDOW_HSPLIT) {
                window_grow_x(p, dx);
                return;
        }

        if (w == p->left) {
                // we are on the left, so we don't need to translate
                // our side of the .
                propagate(w, 0, 0, dx, 0);
                propagate(p->right, dx, 0, -dx, 0);
        } else {
                // here we _do_ need to translate our side
                propagate(w, -dx, 0, dx, 0);
                propagate(p->left, 0, 0, -dx, 0);
        }
}

void
window_set_height(struct window *w, int height)
{
        window_grow_y(w, height - w->height);
}

void
window_set_width(struct window *w, int width)
{
        window_grow_x(w, width - w->width);
}

void
window_vsplit(struct window *w, struct buffer *buffer)
{
        assert(w->type == WINDOW_WINDOW);

        int topheight = w->height / 2;
        int botheight = w->height - topheight;

        unsigned id = w->id;
        int color = w->color;
        struct buffer *b = w->buffer;

        delwin(w->window);

        w->type = WINDOW_VSPLIT;
        w->top = window_new(w, w->x, w->y, w->width, topheight, color);
        w->bot = window_new(w, w->x, w->y + topheight, w->width, botheight, colors_next(color));

        w->top->id = id;
        w->top->buffer = b;
        w->top->color = color;

        w->bot->buffer = buffer;

        refreshdimensions(w->top);
        refreshdimensions(w->bot);

        w->force_redraw = true;
}

void
window_hsplit(struct window *w, struct buffer *buffer)
{
        assert(w->type == WINDOW_WINDOW);

        int leftwidth = w->width / 2;
        int rightwidth = w->width - leftwidth;

        unsigned id = w->id;
        struct buffer *b = w->buffer;
        int color = w->color;

        delwin(w->window);

        w->type = WINDOW_HSPLIT;
        w->left = window_new(w, w->x, w->y, leftwidth, w->height, color);
        w->right = window_new(w, w->x + leftwidth, w->y, rightwidth, w->height, colors_next(color));

        w->left->id = id;
        w->left->buffer = b;
        w->left->color = color;

        w->right->buffer = buffer;

        refreshdimensions(w->left);
        refreshdimensions(w->right);

        w->force_redraw = true;
}

void
window_delete(struct window *w)
{
        assert(w->parent != NULL);

        struct window *parent = w->parent;
        struct window *sibling = WINDOW_SIBLING(w);

        parent->type = sibling->type;

        if (sibling->type == WINDOW_WINDOW) {
                parent->buffer = sibling->buffer;
                parent->id = sibling->id;
                parent->window = sibling->window;
                reconstruct_curses_window(parent);
        } else {
                parent->window = sibling->window;
                parent->one = sibling->one;
                parent->two = sibling->two;
                parent->one->parent = parent;
                parent->two->parent = parent;
                fixtree(parent);
        }

        refreshdimensions(parent);

        free(sibling);
        delete(w);
}

struct window *
window_search(struct window *w, int id)
{
        if (w == NULL)
                return NULL;

        struct window *r;

        switch (w->type) {
        case WINDOW_WINDOW:
                r = (w->id == id) ? w : NULL;
                break;
        case WINDOW_HSPLIT:
        case WINDOW_VSPLIT:
                (r = window_search(w->one, id)) || (r = window_search(w->two, id));
                break;
        }

        return r;
}
