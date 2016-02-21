#include <errno.h>
#include <string.h>
#include <assert.h>
#include <signal.h>
#include <stdnoreturn.h>
#include <ncurses.h>
#include <locale.h>

#include <tickit.h>

#include "panic.h"
#include "editor.h"
#include "buffer.h"
#include "render.h"
#include "window.h"
#include "log.h"

noreturn static void
quit(struct editor *e)
{
        editor_destroy_all_buffers(e);
        exit(EXIT_SUCCESS);
}

static int
handle_term_input_event(TickitTerm *term, TickitEventType _, void *info, void *ctx)
{
        TickitKeyEventInfo *event = info;
        struct editor *e = ctx;

        if (event->type == TICKIT_KEYEV_KEY) {
                if (strcmp(event->str, "C-e") == 0) {
                        quit(e);
                } else if (strcmp(event->str, "Enter") == 0) {
                        editor_handle_text_input(e, "\n");
                } else if (strcmp(event->str, "Right") == 0) {
                        window_grow_x(e->root_window->top->left, 4);
                } else if (strcmp(event->str, "Down") == 0) {
                        window_grow_y(e->root_window->top->left, 4);
                } else if (strcmp(event->str, "Left") == 0) {
                        window_grow_x(e->root_window->top->left, -4);
                } else if (strcmp(event->str, "Up") == 0) {
                        window_grow_y(e->root_window->top->left, -4);
                }
        } else /* event->type == TICKIT_KEYEV_TEXT */ {
                LOG("Text event: %s", event->str);
                editor_handle_text_input(e, event->str);
        }

        return 0;
}

int main(void)
{
        setlocale(LC_ALL, "");

        initscr();
        noecho();
        cbreak();
        refresh();

        TickitTerm *term = tickit_term_open_stdio();
        if (term == NULL) {
                panic("failed to open a TickitTerm instance: %s", strerror(errno));
        }

        tickit_term_await_started_msec(term, 100);
        tickit_term_setctl_int(term, TICKIT_TERMCTL_CURSORVIS, 1);
        tickit_term_setctl_int(term, TICKIT_TERMCTL_ALTSCREEN, 1);

        int lines, cols;
        tickit_term_get_size(term, &lines, &cols);

        struct editor e = editor_new(lines, cols);
        tickit_term_bind_event(term, TICKIT_EV_KEY, 0, handle_term_input_event, &e);

        struct window *w = e.root_window;

        window_vsplit(w);
        window_hsplit(w->top);
        //window_grow_x(w->top->left, 30);
        window_grow_y(w->top->left, 10);

        e.current_window = w->top->left;

        unsigned b1 = editor_create_file_buffer(&e, "foobar.txt");
        unsigned b2 = editor_create_file_buffer(&e, "src/editor.c");
        //unsigned b3 = editor_create_file_buffer(&e, "bar.txt");
        unsigned b3 = editor_create_file_buffer(&e, "src/str.c");

        editor_view_buffer(&e, w->top->left, b1);
        editor_view_buffer(&e, w->top->right, b2);
        editor_view_buffer(&e, w->bot, b3);


        for (;;) {
                render(&e, term);
                tickit_term_input_wait_msec(term, 10);
        }

        return 0;
}
