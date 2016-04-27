#include <stdlib.h>
#include <assert.h>

#include "protocol.h"
#include "window.h"
#include "alloc.h"
#include "test.h"
#include "buffer.h"
#include "log.h"
#include "colors.h"
#include "util.h"

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

	w->redraw = true;
}

inline static void
fixwin(struct window *w)
{
        wresize(w->window, w->height, w->width);
        mvwin(w->window, w->y, w->x);
}

static void
hbalance(struct window *w)
{
        switch (w->type) {
        case WINDOW_VSPLIT:
                w->one->width = w->width;
                w->two->width = w->width;
                w->one->x = w->x;
                w->two->x = w->x;
                break;
        case WINDOW_HSPLIT:
                w->one->width = (w->width / 2) + (w->width & 1);
                w->two->width = w->width - w->one->width;
                w->one->x = w->x;
                w->two->x = w->x + w->one->width;
                break;
        case WINDOW_WINDOW:
                fixwin(w);
                refreshdimensions(w);
                return;
        }

        hbalance(w->one);
        hbalance(w->two);
}

static void
vbalance(struct window *w)
{
        switch (w->type) {
        case WINDOW_VSPLIT:
                w->one->height = (w->height / 2) + (w->height & 1);
                w->two->height = w->height - w->one->height;
                w->one->y = w->y;
                w->two->y = w->y + w->one->height;
                break;
        case WINDOW_HSPLIT:
                w->one->height = w->height;
                w->two->height = w->height;
                w->one->y = w->y;
                w->two->y = w->y;
                break;
        case WINDOW_WINDOW:
                fixwin(w);
                refreshdimensions(w);
                return;
        }

        vbalance(w->top);
        vbalance(w->bot);
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
                fixwin(w);
                refreshdimensions(w);
                return;
        }

        fixtree(w->one);
        fixtree(w->two);
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
        w->redraw = true;
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
        struct window *p = w->parent;

        while (p != NULL && p->type != WINDOW_VSPLIT) {
                w = p;
                p = p->parent;
        }

        if (p == NULL)
                return;

        w->height += dy;
        WINDOW_SIBLING(w)->height -= dy;

        if (w == p->top)
                p->bot->y += dy;
        else
                w->y -= dy;

        vbalance(p->top);
        vbalance(p->bot);
}

void
window_grow_x(struct window *w, int dx)
{
        struct window *p = w->parent;

        while (p != NULL && p->type != WINDOW_HSPLIT) {
                w = p;
                p = p->parent;
        }

        if (p == NULL)
                return;

        w->width += dx;
        WINDOW_SIBLING(w)->width -= dx;

        if (w == p->left)
                p->right->x += dx;
        else
                w->x -= dx;

        hbalance(p->left);
        hbalance(p->right);
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

        w->redraw = true;
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

        w->redraw = true;
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
                fixwin(parent);
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

void
window_touch(struct window *w)
{
        switch (w->type) {
        case WINDOW_HSPLIT:
        case WINDOW_VSPLIT:
                window_touch(w->one);
                window_touch(w->two);
        case WINDOW_WINDOW:
                w->redraw = true;
        }
}

void
window_resize(struct window *w, int height, int width)
{
        w->height = height;
        w->width = width;
        fixtree(w);
}

void
window_cycle_color(struct window *w)
{
        w->color = colors_next(w->color);
        wbkgdset(w->window, COLOR_PAIR(w->color));
        w->redraw = true;
}
