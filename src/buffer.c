#include <stdio.h>
#include <stdbool.h>
#include <errno.h>
#include <string.h>
#include <stdnoreturn.h>
#include <assert.h>
#include <stdarg.h>

#include <poll.h>
#include <sys/stat.h>

#include <sys/mman.h>
#ifndef MAP_ANONYMOUS
#define MAP_ANONYMOUS MAP_ANON
#endif

#include <unistd.h>
#include <fcntl.h>

#include "config.h"
#include "state.h"
#include "alloc.h"
#include "buffer.h"
#include "functions.h"
#include "tb.h"
#include "location.h"
#include "test.h"
#include "util.h"
#include "panic.h"
#include "buffer.h"
#include "protocol.h"
#include "subprocess.h"
#include "log.h"
#include "vm.h"

static char buffer[4096];
static char shortpath[4096];
static char fullpath[4096];

enum {
        BUFFER_RENDERBUFFER_SIZE = 65536,
};

static struct tb data;
static bool backgrounded;

/*
 * Used to restart the command loop after a VM panic.
 */
jmp_buf buffer_err_jb;

static char *rb1;
static char *rb2;
static bool *rb_idx;
static bool *rb_changed;
static pthread_mutex_t *rb_mtx;
static bool rb_locked = false;

struct state state;

/* read from this to receive data from the parent */
static int read_fd;

/* write to this to send data to the parent */
static int write_fd;

/* the id of this buffer process */
static int bufid;

/*
 * fds that we poll in the main buffer loop.
 * The fd for reading events from the editor,
 * and any fds for getting output from spawned
 * subprocesses are in here.
 */
static vec(struct pollfd) pollfds;

/*
 * Dimensions of the window viewing this buffer.
 * If this buffer is backgrounded, these values have no meaning.
 */
static int lines;
static int cols;

/*
 * Line and column offsets.
 */
static struct location scroll = { 0, 0 };

 /* remove the ith pollfd struct from the list of pollfds. */
inline static void
rempollfd(int i)
{
        pollfds.items[i] = *vec_last(pollfds);
        --pollfds.count;
}

 /* remove the pollfd struct with fd 'fd' from the list of pollfds. */
inline static int
findpollfd(int fd)
{
        /* start at 1 since the main editor pipe fd will always be at index 0 */
        for (int i = 1; i < pollfds.count; ++i)
                if (pollfds.items[i].fd == fd)
                        return i;
        return -1;
}

/* add 'fd' to the list of fds to poll */
inline static void
addpollfd(int fd)
{
        vec_push(pollfds, ((struct pollfd){ .fd = fd, .events = POLLIN }));
}

inline static void
checkinput(void)
{
        int status;
        struct key key;
        struct value action;

        while ((status = state_handle_input(&state, &action, &key)) != STATE_NOTHING) {
                switch (status) {
                case STATE_ACTION_READY:
                        vm_eval_function(&action, NULL);
                        break;
                case STATE_NOT_BOUND:
                        if (state.mode == STATE_INSERT) {
                                tb_insert(&data, key.str, strlen(key.str));
                        }
                        break;
                }
        }
}

inline static void
rb_lock(void)
{
        if (!rb_locked) {
                pthread_mutex_lock(rb_mtx);
                rb_locked = true;
        }
}

inline static void
rb_unlock(void)
{
        if (rb_locked) {
                pthread_mutex_unlock(rb_mtx);
                rb_locked = false;
        }
}

inline static char *
rb_current(void)
{
        return (*rb_idx) ? rb1 : rb2;
}


inline static void
rb_swap(void)
{
        rb_lock();
        ++*rb_changed;
        --*rb_idx;
        rb_unlock();
}

/*
 * Compute the location of the cursor on the screen based on the cursor location
 * in the buffer and the scroll location.
 */
inline static struct location
screenlocation(void)
{
        return (struct location) {
                .line = tb_line(&data) - scroll.line,
                .col  = tb_column(&data)  - scroll.col
        };
}

/*
 * Called when the screen is scrolled to ensure that the cursor is on a visible line.
 */
inline static void
adjust_cursor(void)
{
        if (tb_line(&data) < scroll.line) {
                tb_down(&data, scroll.line - tb_line(&data));
        } else if (tb_line(&data) + 1 > scroll.line + lines) {
                tb_up(&data, tb_line(&data) + 1 - scroll.line - lines);
        }
}

/*
 * Called when the cursor moves to ensure that the cursor is on a visible line.
 */
inline static void
adjust_scroll(void)
{
        if (tb_line(&data) < scroll.line) {
                scroll.line = tb_line(&data);
        } else if (tb_line(&data) + 1 > scroll.line + lines) {
                scroll.line = tb_line(&data) - lines + 1;
        }

        if (scroll.col + cols > tb_column(&data)) {
                scroll.col = max(0, tb_column(&data) - cols + 1);
        } else if (tb_column(&data) + 1 > scroll.col + cols) {
                scroll.col = tb_column(&data) - cols + 1;
        }
}

static void
echo(char const *fmt, ...)
{
        int n;
        va_list args;
        static char b[512];

        va_start(args, fmt);
        n = vsnprintf(b, cols, fmt, args);
        va_end(args);

        evt_send(write_fd, EVT_STATUS_MESSAGE);
        sendint(write_fd, n);
        write(write_fd, b, n);
}

static void
setfile(char const *path, int n)
{
        if (path[0] == '/') {
                memcpy(fullpath, path, n);
                fullpath[n] = '\0';
        } else {
                getcwd(fullpath, sizeof fullpath);
                memcpy(shortpath, path, n);
                int pwdlen = strlen(fullpath);
                fullpath[pwdlen++] = '/';
                memcpy(fullpath + pwdlen, path, n);
                fullpath[pwdlen + n] = '\0';
        }
}

/*
 * Write all of the data necessary to display the contents of this buffer into the shared memory, so
 * that the parent process can read it and update the display.
 *
 * The format is like this:
 *
 * cursor line - int
 * cursor col  - int
 * insert mode - char (0 or 1)
 *
 * # of lines  - int
 *
 * for each line:
 *      number of bytes (n) - int
 *      n bytes
 *
 */
static void
render(void)
{
        char *dst = rb_current();

        struct location screenloc = screenlocation();

        dst = writeint(dst, screenloc.line);
        dst = writeint(dst, screenloc.col);
        *dst++ = (state.mode == STATE_INSERT);

        tb_draw(&data, dst, scroll.line, scroll.col, lines, cols);

        rb_swap();

        evt_send(write_fd, EVT_RENDER);
}

static void
source_init_files(void)
{
        char const *home = getenv("HOME");
        if (home == NULL) {
                echo("failed to source init files: HOME not in environment");
                return;
        }

        sprintf(buffer, "%s/.plum/plum/start.plum", home);

        if (!vm_execute_file(buffer)) {
                LOG("error sourcing init files: %s", vm_error());
        }
}

static void
handle_editor_event(int ev)
{
        int id;
        int bytes;
        static char smallbuf[256];
        static struct value type;

        switch (ev) {
        case EVT_LOG:
                /*
                 * This event will only ever be received in the console buffer (for now).
                 */
                bytes = recvint(read_fd);
                read(read_fd, buffer, bytes);
                tb_end(&data);
                tb_insert(&data, buffer, bytes);
                tb_insert(&data, "\n", 1);
                break;
        case EVT_WINDOW_DIMENSIONS:
                backgrounded = false;
                lines = recvint(read_fd);
                cols = recvint(read_fd);
                adjust_cursor();
                break;
        case EVT_BACKGROUNDED:
                backgrounded = true;
                break;
        case EVT_MESSAGE:
                id = recvint(read_fd);
                bytes = recvint(read_fd);
                read(read_fd, smallbuf, bytes);
                type = STRING_NOGC(smallbuf, bytes);
                bytes = recvint(read_fd);
                if (bytes == -1) {
                        state_handle_message(&state, INTEGER(id), type, NIL);
                } else {
                        read(read_fd, buffer, bytes);
                        state_handle_message(&state, INTEGER(id), type, STRING_CLONE(buffer, bytes));
                }
                break;
        case EVT_RUN_PROGRAM:
                bytes = recvint(read_fd);
                read(read_fd, smallbuf, bytes);
                snprintf(buffer, sizeof buffer - 1, "import %.*s\n", bytes, smallbuf);
                /* TODO: maybe do something useful with the return value of vm_execute here? */
                if (!vm_execute(buffer)) {
                        LOG("ERROR: %s", vm_error());
                }
                break;
        case EVT_INPUT:
                bytes = recvint(read_fd);
                read(read_fd, buffer, bytes);
                buffer[bytes] = '\0';

                if (strcmp(buffer, "C-j") == 0) {
                        char *line = buffer_current_line();
                        sprintf(buffer, "print(%s);", line);
                        tb_insert(&data, "\n", 1);
                        if (strlen(line) == 0) break;
                        if (vm_execute(buffer)) {
                                char const *out = vm_get_output();
                                tb_insert(&data, out, strlen(out));
                        } else if (strstr(vm_error(), "ParseError") != NULL && (sprintf(buffer, "%s\n", line), vm_execute(buffer))) {
                                char const *out = vm_get_output();
                                tb_insert(&data, out, strlen(out));
                        } else {
                                char const *err = vm_error();
                                tb_insert(&data, err, strlen(err));
                                tb_insert(&data, "\n", 1);
                        }
                } else {
                        state_push_input(&state, buffer);
                        checkinput();
                }
                break;
        default:
                LOG("ERROR: INVALID EVENT: %d", ev);
        }
}

noreturn static void
buffer_main(void)
{
        backgrounded = true;

        /*
         * Initialize the list of file descriptiors that we should poll
         * with the read-end of main editor process's pipe.
         */
        addpollfd(read_fd);

        /*
         * Render twice so that both renderbuffers contain valid data.
         */
        render();
        render();

        /*
         * Initialize the VM instance for this process.
         */
        vm_init();
        source_init_files();

        /*
         * Render again, in case sourcing the init file changed the buffer contents.
         */
        render();

        /*
         * The main loop of the buffer process. Wait for event notifications from the parent,
         * and then process them.
         */
        for (;;) {

                /*
                 * Jump here whenever there is a panic in the VM.
                 */
                if (setjmp(buffer_err_jb) != 0) {
                        char const *e = vm_error();
                        int bytes = strlen(e);
                        evt_send(write_fd, EVT_VM_ERROR);
                        sendint(write_fd, bytes);
                        write(write_fd, e, bytes);
                        if (!backgrounded) {
                                adjust_scroll();
                                render();
                        }
                        continue;
                }

                for (;;) {
                        /*
                         * Group all of the edits which take place during a single iteration
                         * of the buffer loop (unless we're in insert mode; then we keep using the
                         * same group).
                         */
                        if (data.changed && state.mode != STATE_INSERT) {
                                tb_start_new_edit(&data);
                                data.changed = false;
                        }

                        int timeout = state_pending_input(&state) ? KEY_CHORD_TIMEOUT_MS : -1;
                        int n = poll(pollfds.items, pollfds.count, timeout);

                        /*
                         * If n is zero, that means we were waiting on user input, so all we
                         * have to do is let the state machine know that it timed out.
                         */
                        if (n == 0) {
                                checkinput();
                                goto next;
                        }

                        /* check for editor events */
                        if (pollfds.items[0].revents & POLLIN)
                                handle_editor_event(evt_recv(read_fd));

                        /* check any subprocesses */
                        for (int i = 1; i < pollfds.count; ++i) {
                                if (pollfds.items[i].revents & (POLLIN | POLLHUP)) {
                                        int r = read(pollfds.items[i].fd, buffer, sizeof buffer);
                                        if (r == 0) {
                                                int fd = pollfds.items[i].fd;
                                                rempollfd(i--);
                                                sp_on_exit(fd);
                                        } else {
                                                sp_on_output(pollfds.items[i].fd, buffer, r);
                                        }
                                }
                        }
next:
                        if (!backgrounded) {
                                adjust_scroll();
                                render();
                        }
                }
        }
}

struct buffer
buffer_new(unsigned id)
{
        void *rb1_mem = alloc(BUFFER_RENDERBUFFER_SIZE);
        rb1 = mmap(rb1_mem, BUFFER_RENDERBUFFER_SIZE, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_SHARED, -1, 0);
        if (rb1 == MAP_FAILED) {
                panic("mmap failed: %s", strerror(errno));
        }

        void *rb2_mem = alloc(BUFFER_RENDERBUFFER_SIZE);
        rb2 = mmap(rb2_mem, BUFFER_RENDERBUFFER_SIZE, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_SHARED, -1, 0);
        if (rb2 == MAP_FAILED) {
                panic("mmap failed: %s", strerror(errno));
        }

        void *mtx_mem = alloc(sizeof (pthread_mutex_t));
        rb_mtx = mmap(mtx_mem, sizeof (pthread_mutex_t), PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_SHARED, -1, 0);
        if (rb_mtx == MAP_FAILED) {
                panic("mmap failed: %s", strerror(errno));
        }

        void *idx_mem = alloc(sizeof (bool));
        rb_idx = mmap(idx_mem, sizeof (bool), PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_SHARED, -1, 0);
        if (rb_idx == MAP_FAILED) {
                panic("mmap failed: %s", strerror(errno));
        }

        void *changed_mem = alloc(sizeof (bool));
        rb_changed = mmap(changed_mem, sizeof (bool), PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_SHARED, -1, 0);
        if (rb_changed == MAP_FAILED) {
                panic("mmap failed: %s", strerror(errno));
        }

        /*
         * We start rb_idx as 'true', meaning the parent will read from rb2. When the buffer created, nothing
         * has been rendered yet, so we need to write a 0 to rb2 so that the parent knows not to try reading
         * anything.
         */
        *rb_changed = true;
        *rb_idx = true;
        *(int *)rb2 = 0;


        pthread_mutexattr_t rb_mutexattr;
        pthread_mutexattr_init(&rb_mutexattr);
        pthread_mutexattr_setpshared(&rb_mutexattr, PTHREAD_PROCESS_SHARED);

        if (pthread_mutex_init(rb_mtx, &rb_mutexattr) != 0) {
                panic("pthread_mutex_init failed: %s", strerror(errno));
        }

        pthread_mutexattr_destroy(&rb_mutexattr);

        int p2c[2]; // parent to child pipe
        int c2p[2]; // child to parent pipe
        if (pipe(p2c) != 0 || pipe(c2p) != 0) {
                panic("pipe() failed: %s", strerror(errno));
        }

        pid_t pid;
        if (pid = fork(), pid == -1) {
                panic("fork() failed: %s", strerror(errno));
        }

        if (pid == 0) { // child
                close(c2p[0]);
                close(p2c[1]);

                // lock the renderbuffer mutex and tell the parent
                rb_lock();
                evt_send(c2p[1], EVT_CHILD_LOCKED_MUTEX);

                data = tb_new();
                state = state_new();
                read_fd = p2c[0];
                write_fd = c2p[1];
                bufid = id;

                buffer_main();
        } else {        // parent
                close(c2p[1]);
                close(p2c[0]);

                // wait for the child to lock the renderbuffer mutex
                assert(evt_recv(c2p[0]) == EVT_CHILD_LOCKED_MUTEX);

                return (struct buffer) {
                        .id  = id,
                        .pid = pid,
                        .rb1 = rb1,
                        .rb2 = rb2,
                        .rb_mtx = rb_mtx,
                        .rb_idx = rb_idx,
                        .rb_changed = rb_changed,
                        .read_fd = c2p[0],
                        .write_fd = p2c[1],
                        .window = NULL,
                };
        }
}

char *
buffer_current_line(void)
{
        return tb_clone_line(&data);
}

void
buffer_insert_n(char const *text, int n)
{
        tb_insert(&data, text, n);
}

int
buffer_remove(int n)
{
        return tb_remove(&data, n);
}

void
buffer_clear(void)
{
        tb_clear(&data);
}

void
buffer_clear_to_start(void)
{
        tb_clear_left(&data);
}

void
buffer_clear_to_end(void)
{
        tb_clear_right(&data);
}

int
buffer_forward(int n)
{
        return tb_forward(&data, n);
}

int
buffer_backward(int n)
{
        return tb_backward(&data, n);
}

int
buffer_right(int n)
{
        return tb_right(&data, n);
}

void
buffer_start_of_line(void)
{
        tb_start_of_line(&data);
}

void
buffer_end_of_line(void)
{
        tb_end_of_line(&data);
}

void
buffer_start(void)
{
        tb_start(&data);
}

void
buffer_end(void)
{
        tb_end(&data);
}

int
buffer_left(int n)
{
        return tb_left(&data, n);
}

int
buffer_line(void)
{
        return tb_line(&data);
}

int
buffer_column(void)
{
        return tb_column(&data);
}

int
buffer_lines(void)
{
        return tb_lines(&data);
}

void
buffer_grow_x(int amount)
{
        evt_send(write_fd, EVT_GROW_X);
        sendint(write_fd, amount);
}

void
buffer_grow_y(int amount)
{
        evt_send(write_fd, EVT_GROW_Y);
        sendint(write_fd, amount);
}

struct value
buffer_window_height(void)
{
        if (backgrounded)
                return NIL;
        else
                return INTEGER(lines);
}

struct value
buffer_window_width(void)
{
        if (backgrounded)
                return NIL;
        else
                return INTEGER(cols);
}

int
buffer_next_line(int amount)
{
        return tb_down(&data, amount);
}

int
buffer_prev_line(int amount)
{
        return tb_up(&data, amount);
}

int
buffer_scroll_y(void)
{
        return scroll.line;
}

int
buffer_scroll_x(void)
{
        return scroll.col;
}

int
buffer_scroll_down(int amount)
{
        int max_possible = (tb_lines(&data) - 1) - scroll.line;
        int scrolling = min(amount, max_possible);

        scroll.line += scrolling;
        adjust_cursor();

        return scrolling;
}

int
buffer_scroll_up(int amount)
{
        int max_possible = scroll.line;
        int scrolling = min(amount, max_possible);

        scroll.line -= scrolling;
        adjust_cursor();

        return scrolling;
}

void
buffer_next_window(void)
{
        evt_send(write_fd, EVT_NEXT_WINDOW);
}

void
buffer_prev_window(void)
{
        evt_send(write_fd, EVT_PREV_WINDOW);
}

void
buffer_window_right(void)
{
        evt_send(write_fd, EVT_WINDOW_RIGHT);
}

void
buffer_window_left(void)
{
        evt_send(write_fd, EVT_WINDOW_LEFT);
}

void
buffer_window_down(void)
{
        evt_send(write_fd, EVT_WINDOW_DOWN);
}

void
buffer_window_up(void)
{
        evt_send(write_fd, EVT_WINDOW_UP);
}

void
buffer_goto_window(int id)
{
        evt_send(write_fd, EVT_GOTO_WINDOW);
        sendint(write_fd, id);
}

void
buffer_map_normal(struct value_array *chord, struct value action)
{
        state_map_normal(&state, chord, action);
}

void
buffer_map_insert(struct value_array *chord, struct value action)
{
        state_map_insert(&state, chord, action);
}

void
buffer_normal_mode(void)
{
        state_enter_normal(&state);
}

void
buffer_insert_mode(void)
{
        state_enter_insert(&state);
}

void
buffer_cut_line(void)
{
        tb_truncate_line(&data);
}

struct value
buffer_save_excursion(struct value *f)
{
        int *marker = tb_new_marker(&data, data.character);
        struct value result = vm_eval_function(f, &NIL);
        tb_seek(&data, *marker);
        tb_delete_marker(&data, marker);
        return result;
}

void
buffer_mark_values(void)
{
        state_mark_actions(&state);
}

struct value
buffer_next_char(int i)
{
        if (i == -1)
                return tb_get_char(&data, data.character);
        else
                return tb_get_char(&data, i);
}

struct value
buffer_get_line(int i)
{
        if (i == -1)
                return tb_get_line(&data, data.line);
        else
                return tb_get_line(&data, i);
}

void
buffer_each_line(struct value *f)
{
        tb_each_line(&data, f);
}

int
buffer_point(void)
{
        return data.character;
}

void
buffer_log(char const *s)
{
        int bytes = strlen(s);
        evt_send(write_fd, EVT_LOG);
        sendint(write_fd, bytes);
        write(write_fd, s, bytes);
}

void
buffer_echo(char const *s, int bytes)
{
        evt_send(write_fd, EVT_STATUS_MESSAGE);
        sendint(write_fd, bytes);
        write(write_fd, s, bytes);
}

bool
buffer_undo(void)
{
        return tb_undo(&data);
}

bool
buffer_redo(void)
{
        return tb_redo(&data);
}

void
buffer_center_current_line(void)
{
        scroll.line = max(tb_line(&data) - (lines / 2), 0);
}

bool
buffer_next_match_regex(pcre *re, pcre_extra *extra)
{
        return tb_next_match_regex(&data, re, extra);
}

bool
buffer_next_match_string(char const *s, int n)
{
        return tb_next_match_string(&data, s, n);
}

int
buffer_seek(int i)
{
        return tb_seek(&data, i);
}

int
buffer_spawn(char *path, struct value_array *args, struct value on_output, struct value on_exit)
{
        int fds[2];

        if (!sp_tryspawn(path, args, on_output, on_exit, fds)) {
                echo("Failed to spawn process '%s': %s", path, strerror(errno));
                free(path);
                return -1;
        }

        addpollfd(fds[1]);

        return fds[0];
}

bool
buffer_kill_subprocess(int fd)
{
        if (!sp_fdvalid(fd))
                return false;

        sp_kill(fd);

        return true;
}

bool
buffer_wait_subprocess(int fd)
{
        if (!sp_fdvalid(fd))
                return false;

        sp_wait(fd);

        return true;
}

/*
 * Just closes our end of the pipe.
 */
bool
buffer_close_subprocess(int fd)
{
        if (!sp_fdvalid(fd))
                return false;

        sp_close(fd);

        return true;
}

bool
buffer_write_to_subprocess(int fd, char const *data, int n)
{
        if (!sp_fdvalid(fd))
                return false;

        /* maybe this should be done in subprocess.c just for the sake of encapsulation */
        if (write(fd, data, n) == -1)
                return false;

        return true;
}

void
buffer_write_file(char const *path, int n)
{
        if (n >= sizeof buffer) {
                echo("Filename '%.10s...' is too long. Not writing.", path);
                return;
        }

        memcpy(buffer, path, n);
        buffer[n] = '\0';

        int fd = open(buffer, O_WRONLY | O_TRUNC | O_CREAT, 0666);
        if (fd == -1) {
                echo("Failed to open %s for writing: %s", buffer, strerror(errno));
                return;
        }

        int bytes = tb_write(&data, fd);
        close(fd);

        setfile(path, n);
        echo("Wrote %.*s (%d bytes)", n, path, bytes);
}

void
buffer_load_file(char const *path, int n)
{
        if (n >= sizeof buffer) {
                echo("Filename '%.10s...' is too long. Not reading.", path);
                return;
        }

        memcpy(buffer, path, n);
        buffer[n] = '\0';

        int fd = open(buffer, O_RDONLY | O_CREAT, 0666);
        if (fd == -1) {
                echo("Failed to open %s for reading: %s", buffer, strerror(errno));
        }

        tb_murder(&data);
        data = tb_new();

        tb_read(&data, fd);
        tb_seek(&data, 0);

        close(fd);

        tb_start_history(&data);
        tb_start_new_edit(&data);

        setfile(path, n);
}

void
buffer_save_file(void)
{
        if (fullpath[0] == '\0')
                echo("No file to save...");
        else
                buffer_write_file(fullpath, strlen(fullpath));
}

char const *
buffer_file_name(void)
{
        return fullpath;
}

void
buffer_show_console(void)
{
        evt_send(write_fd, EVT_SHOW_CONSOLE);
}

int
buffer_horizontal_split(int buf, int size)
{
        evt_send(write_fd, EVT_HSPLIT);
        sendint(write_fd, buf);
        sendint(write_fd, size);

        buffer_event_code ev;
        for (;;) {
                ev = evt_recv(read_fd);
                if (ev == EVT_WINDOW_ID)
                        return recvint(read_fd);
                else
                        handle_editor_event(ev);
        }
}

int
buffer_vertical_split(int buf, int size)
{
        evt_send(write_fd, EVT_VSPLIT);
        sendint(write_fd, buf);
        sendint(write_fd, size);

        buffer_event_code ev;
        for (;;) {
                ev = evt_recv(read_fd);
                if (ev == EVT_WINDOW_ID)
                        return recvint(read_fd);
                else
                        handle_editor_event(ev);
        }
}

int
buffer_window_id(void)
{
        evt_send(write_fd, EVT_WINDOW_ID);

        buffer_event_code ev;
        for (;;) {
                ev = evt_recv(read_fd);
                if (ev == EVT_WINDOW_ID)
                        return recvint(read_fd);
                else
                        handle_editor_event(ev);
        }
}

void
buffer_delete_window(void)
{
        evt_send(write_fd, EVT_WINDOW_DELETE);
}

void
buffer_send_message(int id, char const *type, int tn, char const *msg, int mn)
{
        evt_send(write_fd, EVT_MESSAGE);

        sendint(write_fd, id);

        sendint(write_fd, tn);
        write(write_fd, type, tn);

        sendint(write_fd, mn);
        if (mn != -1) {
                write(write_fd, msg, mn);
        }
}

void
buffer_register_message_handler(struct value type, struct value f)
{
        state_register_message_handler(&state, type, f);
}

int
buffer_id(void)
{
        return bufid;
}

void
buffer_cycle_window_color(void)
{
        evt_send(write_fd, EVT_WINDOW_CYCLE_COLOR);
}

/*
 * Try to spawn a new buffer and run the program specified by
 * 'prog', which is a pointer into an n-byte string.
 *
 * Alternatively, n can be -1, and 'prog' will be ignored.
 *
 * example: buffer_create("foo::bar", 11);
 *
 * will try to spawn a new buffer and in the new buffer, run
 * the program $HOME/.plum/foo/bar.plum
 */
int
buffer_create(char const *prog, int n)
{
        evt_send(write_fd, EVT_NEW_BUFFER);

        sendint(write_fd, n);
        if (n != -1)
                write(write_fd, prog, n);

        buffer_event_code ev;
        for (;;) {
                ev = evt_recv(read_fd);
                if (ev == EVT_NEW_BUFFER)
                        return recvint(read_fd);
                else
                        handle_editor_event(ev);
        }
}

int
buffer_mode(void)
{
        return state.mode;
}

bool
buffer_write_to_proc(int p)
{
        if (!sp_fdvalid(p))
                return false;

        tb_write(&data, p);

        return true;
}

void
buffer_source(void)
{
        char *source = tb_cstr(&data);
        if (!vm_execute(source)) {
        }
        free(source);
}

void
buffer_goto_line(int i)
{
        tb_seek_line(&data, i);
}

struct value
buffer_get_char(void)
{
        char b[16];

        buffer_event_code ev;
        for (;;) {
                ev = evt_recv(read_fd);
                if (ev == EVT_INPUT) {
                        int n = recvint(read_fd);
                        read(read_fd, b, n);
                        return STRING_CLONE(b, n);
                } else {
                        handle_editor_event(ev);
                }
        }
}

bool
buffer_find_forward(char const *s, int n)
{
        return tb_find_next(&data, s, n);
}

bool
buffer_find_backward(char const *s, int n)
{
        return tb_find_prev(&data, s, n);
}

void
blog(char const *fmt, ...)
{
        va_list ap;
        va_start(ap, fmt);

        vsnprintf(buffer, sizeof buffer - 1, fmt, ap);

        va_end(ap);

        buffer_log(buffer);
}
