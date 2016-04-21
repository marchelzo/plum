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

static TickitTerm *term;

noreturn static void
quit(struct editor *e)
{
        editor_destroy_all_buffers(e);

        /*
         * Restore the cursor shape if it's been changed.
         */
        write(1, INSERT_END_STRING, sizeof INSERT_END_STRING - 1);

        tickit_term_destroy(term);
        endwin();

        exit(EXIT_SUCCESS);
}

static int
handle_term_input_event(TickitTerm *term, TickitEventType _, void *info, void *ctx)
{
        TickitKeyEventInfo *event = info;
        struct editor *e = ctx;

        if (event->type == TICKIT_KEYEV_KEY) {
                if (strcmp(event->str, "C-d") == 0) {
                        quit(e);
                } else {
                        editor_handle_key_input(e, event->str);
                }
        } else /* event->type == TICKIT_KEYEV_TEXT */ {
                editor_handle_text_input(e, event->str);
        }

        return 0;
}

int main(void)
{
        /*
         * Necessary for curses to be able to draw multi-byte characters properly.
         */
        setlocale(LC_ALL, "");

        initscr();
        noecho();
        raw();

        term = tickit_term_open_stdio();
        if (term == NULL) {
                panic("failed to open a TickitTerm instance: %s", strerror(errno));
        }

        tickit_term_await_started_msec(term, 100);
        tickit_term_setctl_int(term, TICKIT_TERMCTL_CURSORVIS, 1);

        int lines, cols;
        tickit_term_get_size(term, &lines, &cols);

        struct editor e;
        editor_init(&e, lines, cols);

        tickit_term_bind_event(term, TICKIT_EV_KEY, 0, handle_term_input_event, &e);

        unsigned b1 = editor_create_file_buffer(&e, "");
        editor_view_buffer(&e, e.current_window, b1);

        for (;;) {
                render(&e);
                tickit_term_input_wait_msec(term, 10);
                editor_do_update(&e);
        }

        return 0;
}
