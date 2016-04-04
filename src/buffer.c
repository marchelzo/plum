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
#include <unistd.h>
#include <fcntl.h>

#include "config.h"
#include "state.h"
#include "alloc.h"
#include "buffer.h"
#include "editor_functions.h"
#include "tb.h"
#include "location.h"
#include "test.h"
#include "util.h"
#include "panic.h"
#include "file.h"
#include "buffer.h"
#include "protocol.h"
#include "subprocess.h"
#include "log.h"
#include "vm.h"

static int
buffer_load_file(char const *);

enum {
        BUFFER_RENDERBUFFER_SIZE = 65536,
        BUFFER_LOAD_OK,
        BUFFER_LOAD_NO_PERMISSION,
        BUFFER_LOAD_NO_SUCH_FILE
};

static char filename[512];

static struct file file;
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

/*
 * Input events received from the parent process are read into this
 * buffer. Input events are either text events or key events, none
 * of which should exceed 64 bytes. We will see, though.
 */
static char input_buffer[64];

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
 * This is called whenever the window dimensions change, and it ensures
 * that the cursor is always on a line which is visible in the buffer.
 */
inline static void
updatedimensions(int newlines, int newcols)
{
        int dy = newlines - lines;
        int dx = newcols - cols;

        if (dy > 0) {
                int yscroll = scroll.line;
                // not sure if this is useful or not
                //yscroll -= max(newlines - (tb_lines(&data) - yscroll), 0);
                yscroll -= (dy + 1) / 2;
                scroll.line = max(yscroll, 0);
        }
        if (tb_line(&data) >= scroll.line + newlines) {
                scroll.line = tb_line(&data) - newlines + 1;
        }

        if (dx > 0) {
                int xscroll = scroll.col;
                xscroll -= max(newcols - (tb_line_width(&data) - xscroll), 0);
                xscroll -= (dx + 1) / 2;
                scroll.col = max(xscroll, 0);
                scroll.col = 0;
        }
        if (tb_column(&data) >= scroll.col + newcols) {
                scroll.col = tb_column(&data) - newcols + 1;
        }

        lines = newlines;
        cols = newcols;
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

inline static char *
renderstatus(char *dst)
{
        char *status_size = dst;
        dst += sizeof (int);

        int n = sprintf(
                dst,
                " %-*s%d,%d",
                cols - 20,
                file.path == NULL ? "[No Name]" : file.path,
                tb_line(&data) + 1,
                tb_column(&data) + 1
        );

        n += sprintf(dst + n, "%*s", cols - n, " ");

        memcpy(status_size, &n, sizeof (int));
        dst += n;

        return dst;
}

/*
 * Write all of the data necessary to display the contents of this buffer into the shared memory, so
 * that the parent process can read it and update the display.
 *
 * The format is like this:
 *
 * cursor line - int
 * cursor col  - int
 * insert mode - int (0 or 1)
 *
 * # of line   - int
 *
 * for each line:
 *      number of bytes (n) - int
 *      n bytes
 */
static void
render(void)
{
        char *dst = rb_current();

        struct location screenloc = screenlocation();

        dst = renderstatus(dst);

        dst = writeint(dst, screenloc.line);
        dst = writeint(dst, screenloc.col);
        *dst++ = (state.mode == STATE_INSERT);

        tb_draw(&data, dst, scroll.line, scroll.col, lines, cols);

        rb_swap();
}

static void
source_init_files(void)
{
        char const *home = getenv("HOME");
        char buffer[512];

        sprintf(buffer, "%s/.plum/plum/start.plum", home);

        if (!vm_execute_file(buffer)) {
                LOG("error was: %s", vm_error());
        }
}

static void
handle_editor_event(int ev)
{
        int bytes;
        int newlines, newcols;
        char buf[4096];

        switch (ev) {
        case EVT_LOAD_FILE: {
                int size = recvint(read_fd);
                char *path = alloc(size + 1);
                path[size] = '\0';
                read(read_fd, path, size);

                buffer_load_file(path);

                /*
                 * Start recording changes, and start a new edit group.
                 */
                tb_start_history(&data);
                tb_start_new_edit(&data);

                break;
        }
        case EVT_LOG_REQUEST:
                /*
                 * This event will only ever be received in the console buffer (for now).
                 */
                bytes = recvint(read_fd);
                read(read_fd, buf, bytes);
                tb_end(&data);
                tb_insert(&data, buf, bytes);
                tb_insert(&data, "\n", 1);
                break;
        case EVT_WINDOW_DIMENSIONS:
                backgrounded = false;
                newlines = recvint(read_fd);
                newcols = recvint(read_fd);
                updatedimensions(newlines, newcols);
                break;
        case EVT_TEXT_INPUT:
                bytes = recvint(read_fd);
                read(read_fd, input_buffer, bytes);
                input_buffer[bytes] = '\0';
                state_push_input(&state, input_buffer);
                checkinput();
                break;
        case EVT_KEY_INPUT:
                bytes = recvint(read_fd);
                read(read_fd, input_buffer, bytes);
                input_buffer[bytes] = '\0';

                LOG("KEY = '%s'", input_buffer);

                if (strcmp(input_buffer, "C-j") == 0) {
                        char *line = buffer_current_line();
                        sprintf(buf, "print(%s);", line);
                        tb_insert(&data, "\n", 1);
                        if (strlen(line) == 0) break;
                        if (vm_execute(buf)) {
                                char const *out = vm_get_output();
                                tb_insert(&data, out, strlen(out));
                        } else if (strstr(vm_error(), "ParseError") != NULL && (sprintf(buf, "%s\n", line), vm_execute(buf))) {
                                char const *out = vm_get_output();
                                tb_insert(&data, out, strlen(out));
                        } else {
                                char const *err = vm_error();
                                tb_insert(&data, err, strlen(err));
                                tb_insert(&data, "\n", 1);
                        }
                } else {
                        state_push_input(&state, input_buffer);
                        checkinput();
                }
        }
}

noreturn static void
buffer_main(void)
{
        backgrounded = true;

        /*
         * Initialize the VM instance for this process.
         */
        vm_init();
        source_init_files();

        /*
         * Initialize the list of file descriptiors that we should poll
         * with the read-end of main editor process's pipe.
         */
        addpollfd(read_fd);

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
                        static char proc_out_buf[4096];
                        for (int i = 1; i < pollfds.count; ++i) {
                                if (pollfds.items[i].revents & POLLIN) {
                                        int r = read(pollfds.items[i].fd, proc_out_buf, sizeof proc_out_buf);
                                        if (r == 0) {
                                                int fd = pollfds.items[i].fd;
                                                rempollfd(i--);
                                                sp_on_exit(fd);
                                        } else {
                                                sp_on_output(pollfds.items[i].fd, proc_out_buf, r);
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
        rb1 = mmap(rb1_mem, BUFFER_RENDERBUFFER_SIZE, PROT_READ | PROT_WRITE, MAP_ANON | MAP_SHARED, -1, 0);
        if (rb1 == MAP_FAILED) {
                panic("mmap failed: %s", strerror(errno));
        }

        void *rb2_mem = alloc(BUFFER_RENDERBUFFER_SIZE);
        rb2 = mmap(rb2_mem, BUFFER_RENDERBUFFER_SIZE, PROT_READ | PROT_WRITE, MAP_ANON | MAP_SHARED, -1, 0);
        if (rb2 == MAP_FAILED) {
                panic("mmap failed: %s", strerror(errno));
        }

        void *mtx_mem = alloc(sizeof (pthread_mutex_t));
        rb_mtx = mmap(mtx_mem, sizeof (pthread_mutex_t), PROT_READ | PROT_WRITE, MAP_ANON | MAP_SHARED, -1, 0);
        if (rb_mtx == MAP_FAILED) {
                panic("mmap failed: %s", strerror(errno));
        }

        void *idx_mem = alloc(sizeof (bool));
        rb_idx = mmap(idx_mem, sizeof (bool), PROT_READ | PROT_WRITE, MAP_ANON | MAP_SHARED, -1, 0);
        if (rb_idx == MAP_FAILED) {
                panic("mmap failed: %s", strerror(errno));
        }

        void *changed_mem = alloc(sizeof (bool));
        rb_changed = mmap(changed_mem, sizeof (bool), PROT_READ | PROT_WRITE, MAP_ANON | MAP_SHARED, -1, 0);
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
                file = file_invalid();
                state = state_new();
                read_fd = p2c[0];
                write_fd = c2p[1];

                buffer_main();
        } else {        // parent
                close(c2p[1]);
                close(p2c[0]);

                // wait for the child to lock the renderbuffer mutex
                assert(evt_recv(c2p[0]) == EVT_CHILD_LOCKED_MUTEX);

                /*
                 * Now we go into non-blocking mode, because the only time we will read
                 * from this pipe is when polling for input events in the main editor loop.
                 */
                fcntl(c2p[0], F_SETFL, O_NONBLOCK);

                LOG("RETURNING BUFFER WITH ID %d: rb_idx = %d", (int) id, (int) *rb_idx);
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

static int
buffer_load_file(char const *path)
{
        int fd = open(path, O_RDONLY);
        if (fd == -1) {
                switch (errno) {
                case EACCES: return BUFFER_LOAD_NO_PERMISSION;
                case ENOENT: return BUFFER_LOAD_NO_SUCH_FILE;
                default:     panic("unknown error");
                }
        }

        file = file_new(path);

        tb_read(&data, fd);
        tb_seek(&data, 0);

        close(fd);
        
        return BUFFER_LOAD_OK;
}

char *
buffer_current_line(void)
{
        return tb_clone_line(&data);
}

/*
 * Insert 'n' bytes from the data pointed to by 'text' into the buffer at the current cursor position.
 */
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
        evt_send(write_fd, EVT_GROW_X_REQUEST);
        sendint(write_fd, amount);
}

void
buffer_grow_y(int amount)
{
        evt_send(write_fd, EVT_GROW_Y_REQUEST);
        sendint(write_fd, amount);
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
buffer_scroll_down(int amount)
{
        // TODO: allow scrolling further than the last screen height

        int max_possible = (tb_lines(&data) - lines) - scroll.line;
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
        evt_send(write_fd, EVT_NEXT_WINDOW_REQUEST);
}

void
buffer_prev_window(void)
{
        evt_send(write_fd, EVT_PREV_WINDOW_REQUEST);
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
buffer_source_file(char const *path, int bytes)
{
        memcpy(filename, path, bytes);
        filename[bytes] = '\0';
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
buffer_get_char(int i)
{
        if (i == -1) // -1 means current char. this is ugly.
                return tb_get_char(&data, data.character);
        else
                return tb_get_char(&data, i);
}

struct value
buffer_get_line(int i)
{
        if (i == -1) // -1 means current char. this is ugly.
                return tb_get_line(&data, data.line);
        else
                return tb_get_line(&data, i);
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
        evt_send(write_fd, EVT_LOG_REQUEST);
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
buffer_next_match(pcre *re, pcre_extra *extra)
{
        return tb_next_match(&data, re, extra);
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
                blog("Failed to spawn process '%s': %s", path, strerror(errno));
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

        LOG("wrote %d bytes to subprocess", n);

        return true;
}

void
buffer_write_file(char const *path, int n)
{
        static char pathbuf[1024];

        if (n >= 1024) {
                blog("Filename is too long. Not writing.");
                return;
        }
        
        memcpy(pathbuf, path, n);
        pathbuf[n] = '\0';

        int fd = open(pathbuf, O_WRONLY | O_CREAT | O_EXLOCK, 0666);
        if (fd == -1) {
                blog("Failed to open %s for writing: %s", pathbuf, strerror(errno));
        }

        tb_write(&data, fd);

        close(fd);
}

bool
buffer_save_file(void)
{
        if (file.path == NULL)
                return false;

        buffer_write_file(file.path, strlen(file.path));

        return true;
}

char const *
buffer_file_name(void)
{
        return file.path;
}

void
buffer_show_console(void)
{
        evt_send(write_fd, EVT_SHOW_CONSOLE_REQUEST);
}

int
buffer_horizontal_split(void)
{
        evt_send(write_fd, EVT_HSPLIT_REQUEST);

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
buffer_vertical_split(void)
{
        evt_send(write_fd, EVT_VSPLIT_REQUEST);

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
buffer_current_window(void)
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
buffer_delete_window(int id)
{
        evt_send(write_fd, EVT_WINDOW_DELETE_REQUEST);
        sendint(write_fd, id);
}

void
buffer_delete_current_window(void)
{
        evt_send(write_fd, EVT_WINDOW_DELETE_CURRENT_REQUEST);
}

void
blog(char const *fmt, ...)
{
        va_list ap;
        va_start(ap, fmt);

        static char logbuf[1024];
        vsnprintf(logbuf, sizeof logbuf - 1, fmt, ap);

        va_end(ap);

        buffer_log(logbuf);
}

TEST(setup)
{
        file = file_invalid();
        data = tb_new();
}

TEST(create) // OFF
{
        if (buffer_load_file("foo.txt") != BUFFER_LOAD_OK) {
                claim(!"couldn't open foo.txt");
        }

        claim(strcmp("foo.txt", file.path) == 0);
}

TEST(nofile)
{
        claim(buffer_load_file("some_file_that_doesnt_exist.txt") == BUFFER_LOAD_NO_SUCH_FILE);
}

TEST(permissions)
{
        claim(buffer_load_file("pfile") == BUFFER_LOAD_NO_PERMISSION);
}

TEST(bigfile) // OFF
{
        buffer_load_file("bigfile.txt");

        tb_seek(&data, 238472);
        tb_insert(&data, "   testing   ", 11);
}
