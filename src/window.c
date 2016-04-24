#include <assert.h>

#include "protocol.h"
#include "window.h"
#include "alloc.h"
#include "test.h"
#include "buffer.h"
#include "log.h"

enum { ONE, TWO };
static unsigned char stack[28];

static int winid = 0;

inline static struct window *
unwind(struct window *w, int i)
{
        while (w->type != WINDOW_WINDOW && i > 0) switch (stack[--i]) {
        case ONE: w = w->one; break;
        case TWO: w = w->two; break;
        }

        while (w->type != WINDOW_WINDOW)
                w = w->one;

        return w;
}

inline static void
fixline(struct window *w)
{
        switch (w->type) {
        case WINDOW_VSPLIT:
                wresize(w->window, 1, w->width);
                mvwin(w->window, w->y + w->top->height, w->x);
                mvwhline(w->window, 0, 0, ACS_HLINE, w->width);
                break;
        case WINDOW_HSPLIT:
                wresize(w->window, w->height, 1);
                mvwin(w->window, w->y, w->x + w->left->width);
                mvwvline(w->window, 0, 0, ACS_VLINE, w->height);
                break;
        default: assert(!"fixline called on non-split");
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
                /*
                 * If there is a buffer associated with this window, notify it of the
                 * new window dimensions.
                 */
                if (w->buffer != NULL) {
                        refreshdimensions(w);
			reconstruct_curses_window(w);
                }
                return;
        }
}

inline static void
delete(struct window *w)
{
        delwin(w->window);

        w->buffer->window = NULL;
        evt_send(w->buffer->write_fd, EVT_BACKGROUNDED);

        free(w);
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
        w->window = newwin(height, width, y, x);
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
        assert(w->type == WINDOW_WINDOW);

        struct window *p = w->parent;

        int i = 0;
        while (p != NULL && (p->type != WINDOW_HSPLIT || w == p->right)) {
                stack[i++] = (w == p->one) ? ONE : TWO;
                w = p;
                p = p->parent;
        }

        if (p == NULL)
                return NULL;

        return unwind(p->right, i);

}

struct window *
window_left(struct window *w)
{
        assert(w->type == WINDOW_WINDOW);

        struct window *p = w->parent;

        int i = 0;
        while (p != NULL && (p->type != WINDOW_HSPLIT || w == p->left)) {
                stack[i++] = (w == p->one) ? ONE : TWO;
                w = p;
                p = p->parent;
        }

        if (p == NULL)
                return NULL;

        return unwind(p->left, i);
}

struct window *
window_up(struct window *w)
{
        assert(w->type == WINDOW_WINDOW);

        struct window *p = w->parent;

        int i = 0;
        while (p != NULL && (p->type != WINDOW_VSPLIT || w == p->top)) {
                stack[i++] = (w == p->one) ? ONE : TWO;
                w = p;
                p = p->parent;
        }

        if (p == NULL)
                return NULL;

        return unwind(p->top, i);
}

struct window *
window_down(struct window *w)
{
        assert(w->type == WINDOW_WINDOW);

        struct window *p = w->parent;

        int i = 0;
        while (p != NULL && (p->type != WINDOW_VSPLIT || w == p->bot)) {
                stack[i++] = (w == p->one) ? ONE : TWO;
                w = p;
                p = p->parent;
        }

        if (p == NULL)
                return NULL;

        return unwind(p->bot, i);
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

        mvwin(p->window, p->y + p->top->height, p->x);
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

        fixline(p);
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
        int botheight = w->height - topheight - 1;

        unsigned id = w->id;
        struct buffer *b = w->buffer;
        delwin(w->window);

        w->type = WINDOW_VSPLIT;
        w->top = window_new(w, w->x, w->y, w->width, topheight);
        w->bot = window_new(w, w->x, w->y + topheight + 1, w->width, botheight);

        w->top->id = id;
        w->top->buffer = b;

        w->bot->buffer = buffer;

        w->window = newwin(1, w->width, w->y + topheight, w->x);
        fixline(w);

        refreshdimensions(w->top);
        refreshdimensions(w->bot);

        w->force_redraw = true;
}

void
window_hsplit(struct window *w, struct buffer *buffer)
{
        assert(w->type == WINDOW_WINDOW);

        int leftwidth = w->width / 2;
        int rightwidth = w->width - leftwidth - 1;

        unsigned id = w->id;
        struct buffer *b = w->buffer;
        werase(w->window);
        delwin(w->window);

        w->type = WINDOW_HSPLIT;
        w->left = window_new(w, w->x, w->y, leftwidth, w->height);
        w->right = window_new(w, w->x + leftwidth + 1, w->y, rightwidth, w->height);

        w->left->id = id;
        w->left->buffer = b;

        w->right->buffer = buffer;

        w->window = newwin(w->height, 1, w->y, w->x + leftwidth);
        fixline(w);

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

        delwin(parent->window);

        if (sibling->type == WINDOW_WINDOW) {
                parent->buffer = sibling->buffer;
                parent->id = sibling->id;
                parent->window = sibling->window;
                reconstruct_curses_window(parent);
        } else {
                parent->one = sibling->one;
                parent->two = sibling->two;
                parent->one->parent = parent;
                parent->two->parent = parent;
                switch (parent->type) {
                case WINDOW_HSPLIT:
                        parent->one->width = parent->width;
                        parent->two->width = parent->width;
                        break;
                case WINDOW_VSPLIT:
                        parent->one->height = parent->height;
                        parent->two->height = parent->height;
                        break;
                }
                reconstruct_curses_window(parent->one);
                reconstruct_curses_window(parent->two);
        }

        parent->type = sibling->type;
        parent->force_redraw = true;

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
                result = (w->id == id) ? w : NULL;
                break;
        case WINDOW_HSPLIT:
        case WINDOW_VSPLIT:
                (r = window_search(w->one, id)) || (r = window_search(w->two, id));
                break;
        }

        return r;
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
        window_vsplit(w, NULL);

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
        window_hsplit(w, NULL);

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

        window_hsplit(w, NULL);

        window_delete(w->left);

        claim(w->type == WINDOW_WINDOW);
        claim(w->height == 20);
        claim(w->width == 41);
}
