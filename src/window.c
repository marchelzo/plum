#include <assert.h>

#include "protocol.h"
#include "window.h"
#include "alloc.h"
#include "test.h"
#include "buffer.h"

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
                /*
                 * If there is a buffer associated with this window, notify it of the
                 * new window dimensions.
                 */
                if (w->buffer != NULL) {
                        evt_send(w->buffer->write_fd, EVT_WINDOW_DIMENSIONS);
                        sendint(w->buffer->write_fd, w->height);
                        sendint(w->buffer->write_fd, w->width);
                }
                return;
        }
}

static void
freewindow(struct window *w)
{
        // TODO: free stuff
}


struct window *
window_new(
        struct window *parent,
        int x,
        int y,
        int width,
        int height
) {
        struct window *w = alloc(sizeof *w);

        w->type = WINDOW_WINDOW;
        w->buffer = NULL;

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
window_next(struct window *w)
{
        struct window *p = w->parent;

        if (p == NULL) {
                return w;
        }

        if (p->one == w) {
                return p->two;
        }

        do {
                p = p->parent;
                w = w->parent;
        } while (p != NULL && p->two == NULL);

        if (p != NULL) {
                return p->two;
        }

        while (w->type != WINDOW_WINDOW) {
                w = w->one;
        }

        return w;
}

struct window *
window_prev(struct window *w)
{
        struct window *p = w->parent;

        if (p == NULL) {
                return w;
        }

        if (p->one == w) {
                return p->two;
        }

        do {
                p = p->parent;
                w = w->parent;
        } while (p != NULL && p->two == NULL);

        if (p != NULL) {
                return p->two;
        }

        while (w->type != WINDOW_WINDOW) {
                w = w->one;
        }

        return w;
}

void
window_grow_y(struct window *w, int dy)
{
        assert(w != NULL);

        if (w->parent->type != WINDOW_VSPLIT) {
                window_grow_y(w->parent, dy);
                return;
        }

        if (w == w->parent->top) {
                // we are on top, so we don't need to translate
                // our side of the .
                propagate(w, 0, 0, 0, dy);
                propagate(w->parent->bot, 0, dy, 0, -dy);
        } else {
                // here we _do_ need to translate our side
                propagate(w, 0, -dy, 0, dy);
                propagate(w->parent->top, 0, 0, 0, -dy);
        }
}

void
window_grow_x(struct window *w, int dx)
{
        assert(w != NULL);
        assert(w->parent != NULL); // can't grow the root window

        if (w->parent->type != WINDOW_HSPLIT) {
                window_grow_x(w->parent, dx);
                return;
        }

        if (w == w->parent->left) {
                // we are on the left, so we don't need to translate
                // our side of the .
                propagate(w, 0, 0, dx, 0);
                propagate(w->parent->right, dx, 0, -dx, 0);
        } else {
                // here we _do_ need to translate our side
                propagate(w, -dx, 0, dx, 0);
                propagate(w->parent->left, 0, 0, -dx, 0);
        }
}

void
window_vsplit(struct window *w)
{
        assert(w->type == WINDOW_WINDOW);

        int topheight = w->height / 2;
        int botheight = w->height - topheight - 1;

        unsigned id = w->id;
        struct buffer *b = w->buffer;

        w->type = WINDOW_VSPLIT;
        w->top = window_new(w, w->x, w->y, w->width, topheight);
        w->bot = window_new(w, w->x, w->y + topheight + 1, w->width, botheight);

        w->top->id = id;
        w->top->buffer = b;

        w->force_redraw = true;
}

void
window_hsplit(struct window *w)
{
        assert(w->type == WINDOW_WINDOW);

        int leftwidth = w->width / 2;
        int rightwidth = w->width - leftwidth - 1;

        unsigned id = w->id;
        struct buffer *b = w->buffer;

        w->type = WINDOW_HSPLIT;
        w->left = window_new(w, w->x, w->y, leftwidth, w->height);
        w->right = window_new(w, w->x + leftwidth + 1, w->y, rightwidth, w->height);

        w->left->id = id;
        w->left->buffer = b;

        w->force_redraw = true;
}

void
window_delete(struct window *w)
{
        assert(w->parent != NULL);

        struct window *parent = w->parent;
        struct window *sibling = (w == parent->one)
                                   ? parent->two
                                   : parent->one;

        w->parent->type = sibling->type;
        if (sibling->type == WINDOW_WINDOW) {
                parent->buffer = sibling->buffer;
                parent->id = sibling->id;
        } else {
                parent->one = sibling->one;
                parent->two = sibling->two;
        }

        free(sibling);
        freewindow(w);
}

TEST(create)
{
        struct window *w = window_new(NULL, 0, 0, 20, 20);
        claim(w->type == WINDOW_WINDOW);
        claim(w->buffer == NULL);
}

TEST(vsplit)
{
        struct window *w = window_new(NULL, 0, 0, 20, 20);
        window_vsplit(w);

        claim(w->type == WINDOW_VSPLIT);
        claim(w->height == 20);

        claim(w->top != NULL);
        claim(w->bot != NULL);

        claim(w->top->height == 10);
        claim(w->bot->height == 9);

        claim(w->bot->y == 11);
}

TEST(hsplit)
{
        struct window *w = window_new(NULL, 0, 0, 41, 20);
        window_hsplit(w);

        claim(w->type == WINDOW_HSPLIT);
        claim(w->left != NULL);
        claim(w->right != NULL);

        claim(w->width == 41);
        claim(w->left->width == 20);
        claim(w->bot->width == 20);

        claim(w->bot->x == 21);
}

TEST(delete)
{
        struct window *w = window_new(NULL, 0, 0, 41, 20);

        window_hsplit(w);

        window_delete(w->left);

        claim(w->type == WINDOW_WINDOW);
        claim(w->height == 20);
        claim(w->width == 41);
}
