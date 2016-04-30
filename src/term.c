#define _DARWIN_SOURCE

#include <stdnoreturn.h>

#include <errno.h>
#include <signal.h>
#include <curses.h>
#include <termkey.h>
#include <unistd.h>
#include <sys/ioctl.h>

#include "editor.h"
#include "colors.h"
#include "config.h"
#include "log.h"
#include "term.h"

static pid_t pid;
static struct editor *editor;
static TermKey *termkey;

int lines;
int cols;

noreturn static void
quit(struct editor *e)
{
        editor_destroy_all_buffers(e);

        /*
         * Restore the cursor shape if it's been changed.
         */
        write(1, INSERT_END_STRING, sizeof INSERT_END_STRING - 1);

        termkey_stop(termkey);
        endwin();

        exit(EXIT_SUCCESS);
}

static void
suspend(void)
{
        editor_background(editor);
        endwin();
        raise(SIGTSTP);
}


static void
restore()
{
        if (getpid() != pid)
                return;

        struct winsize ws;
        if (ioctl(1, TIOCGWINSZ, &ws) == -1) {
                getmaxyx(stdscr, lines, cols);
        } else {
                lines = ws.ws_row;
                cols = ws.ws_col;
        }

        resizeterm(lines, cols);
        wresize(stdscr, lines, cols);
        werase(stdscr);
        wnoutrefresh(stdscr);

        window_resize(editor->root_window, lines - 1, cols);

        editor_foreground(editor);
}

void
term_init(struct editor *e)
{
        editor = e;
        pid = getpid();

        initscr();
        noecho();
        raw();
        getmaxyx(stdscr, lines, cols);

        if (!colors_init())
                panic("oh no! your terminal doesn't have sufficient color support!");

        termkey = termkey_new(0, TERMKEY_FLAG_UTF8 | TERMKEY_FLAG_CTRLC | TERMKEY_CANON_DELBS);
        if (termkey == NULL)
                panic("failed to open a TermKey instance: %s", strerror(errno));

        signal(SIGCONT, restore);
        signal(SIGWINCH, restore);
}

void
term_handle_input(void)
{
        static TermKeyKey k;
        static char keybuf[64];

        termkey_advisereadable(termkey);

        while (termkey_getkey(termkey, &k) == TERMKEY_RES_KEY) {
                termkey_strfkey(termkey, keybuf, sizeof keybuf, &k, TERMKEY_FORMAT_ALTISMETA);
                if (strcmp(keybuf, "C-d") == 0) {
                        quit(editor);
                } else if (strcmp(keybuf, "C-z") == 0) {
                        suspend();
                } else if (strcmp(keybuf, "DEL") == 0) {
                        editor_handle_input(editor, "Backspace");
                } else {
                        editor_handle_input(editor, keybuf);
                }
        }
}

int
term_height(void)
{
        return lines;
}

int
term_width(void)
{
        return cols;
}
