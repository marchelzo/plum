#include <stdio.h>
#include <stdbool.h>
#include <errno.h>
#include <string.h>
#include <stdnoreturn.h>
#include <assert.h>
#include <tickit.h>

#include <sys/stat.h>
#include <sys/mman.h>
#include <unistd.h>
#include <fcntl.h>

#include "state.h"
#include "alloc.h"
#include "buffer.h"
#include "textbuffer.h"
#include "editor_functions.h"
#include "location.h"
#include "test.h"
#include "util.h"
#include "panic.h"
#include "file.h"
#include "buffer.h"
#include "protocol.h"
#include "log.h"
#include "vm.h"

enum {
        READ_BUFFER_SIZE  = 1 << 14,

        BUFFER_RENDERBUFFER_SIZE = 65536,
        BUFFER_LOAD_OK,
        BUFFER_LOAD_NO_PERMISSION,
        BUFFER_LOAD_NO_SUCH_FILE
};

static int
buffer_load_file(char const *);

static char filename[512];

static struct file file;
static struct textbuffer data;

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
 * Dimensions of the window viewing this buffer.
 * If this buffer is backgrounded, these values have no meaning.
 */
static int lines;
static int cols;

/*
 * The cursor location is not the position of the cursor on the screen, but rather the position
 * in the text buffer representing the file.
 */
static struct location cursor = { 0, 0 };

/* Line and column offsets.
 */
static struct location scroll = { 0, 0 };


/*
 * Input events received from the parent process are read into this
 * buffer. Input events are either text events or key events, none
 * of which should exceed 64 bytes. We will see, though.
 */
static char input_buffer[64];

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
                .line = cursor.line - scroll.line,
                .col  = cursor.col  - scroll.col
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
                intmax_t yscroll = scroll.line;
                yscroll -= max(newlines - (textbuffer_num_lines(&data) - yscroll), 0);
                yscroll -= (dy + 1) / 2;
                scroll.line = max(yscroll, 0);
        }
        if (cursor.line >= scroll.line + newlines) {
                scroll.line = cursor.line - newlines + 1;
        }

        if (dx > 0) {
                intmax_t xscroll = scroll.col;
                xscroll -= max(newcols - (textbuffer_line_width(&data, cursor.line) - xscroll), 0);
                xscroll -= (dx + 1) / 2;
                scroll.col = max(xscroll, 0);
        }
        if (cursor.col >= scroll.col + newcols) {
                scroll.col = cursor.col - newcols + 1;
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
        if (cursor.line < scroll.line) {
                textbuffer_next_line(&data, &cursor, scroll.line - cursor.line);
        } else if (cursor.line + 1 > scroll.line + lines) {
                textbuffer_prev_line(&data, &cursor, cursor.line + 1 - scroll.line - lines);
        }
}

/*
 * Called when the cursor moves to ensure that the cursor is on a visible line.
 */
inline static void
adjust_scroll(void)
{
        if (cursor.line < scroll.line) {
                scroll.line = cursor.line;
        } else if (cursor.line + 1 > scroll.line + lines) {
                scroll.line = cursor.line - lines + 1;
        }

        if (scroll.col + cols > cursor.col) {
                scroll.col = max(0, cursor.col - cols + 1);
        } else if (cursor.col + 1 > scroll.col + cols) {
                scroll.col = cursor.col - cols + 1;
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
 * # of lines  - int
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
        dst = writeint(dst, screenloc.line);
        dst = writeint(dst, screenloc.col);
        dst = writeint(dst, state.mode == STATE_INSERT);

        int lines_to_render = min(textbuffer_num_lines(&data) - scroll.line, lines);

        dst = writeint(dst, lines_to_render);
        for (int i = 0; i < lines_to_render; ++i) {
                dst = textbuffer_copy_line_cols(&data, scroll.line + i, dst, scroll.col, cols);
        }

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

noreturn static void
buffer_main(void)
{
        /*
         * First we wait for instructions about which kind of buffer
         * we are (file / console).
         */
        switch (evt_recv(read_fd)) {
        case EVT_LOAD_FILE: {
                int size = recvint(read_fd);
                char *path = alloc(size + 1);
                path[size] = '\0';
                read(read_fd, path, size);

                buffer_load_file(path);

                break;
        }
        case EVT_START_CONSOLE: {
                /*
                 * Eventually there will be special console buffer in addition to
                 * regular text buffers; like a shell that lets interact with the
                 * plum interpreter directly.
                 */
                break;
        }
        default: panic("buffer process received an invalid event code from parent");
        }

        /*
         * Next we wait to be told the dimensions of the window
         * that is viewing us.
         */
        assert(evt_recv(read_fd) == EVT_WINDOW_DIMENSIONS);
        lines = recvint(read_fd);
        cols = recvint(read_fd);

        /*
         * I don't know if or why this is necessary, or what it even does. Oh well.
         */
        LOG("RENDERING...");
        render();
        render();

        int bytes;
        int newlines, newcols;
        struct key key;
        char buf[4096];
        int status;
        buffer_event_code ev;

        struct value action;

        /*
         * Initialize the VM instance for this process.
         */
        vm_init();
        source_init_files();

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
                        LOG("GOT VM ERROR: %s", e);
                        int bytes = strlen(e);
                        evt_send(write_fd, EVT_VM_ERROR);
                        sendint(write_fd, bytes);
                        write(write_fd, e, bytes);
                        render();
                        continue;
                }

                ev = evt_recv(read_fd);
                switch (ev) {
                case EVT_UPDATE:
                        if ((status = state_handle_input(&state, &action, &key)) != STATE_NOTHING) {
                                switch (status) {
                                case STATE_ACTION_READY:
                                        vm_eval_function(&action, NULL);
                                        break;
                                case STATE_NOT_BOUND:
                                        if (state.mode == STATE_INSERT) {
                                                textbuffer_insert(&data, &cursor, key.str);
                                        }
                                        break;
                                }
                                adjust_scroll();
                                render();
                        }
                        break;
                case EVT_VM_ERROR:
                        /*
                         * This event will only ever be received in the console buffer.
                         */
                        bytes = recvint(read_fd);
                        read(read_fd, buf, bytes);
                        textbuffer_append_n(&data, &cursor, buf, bytes);
                        textbuffer_insert_n(&data, &cursor, "\n", 1);
                        adjust_scroll();
                        render();
                        break;
                case EVT_WINDOW_DIMENSIONS:
                        newlines = recvint(read_fd);
                        newcols = recvint(read_fd);
                        updatedimensions(newlines, newcols);
                        render();
                        break;
                case EVT_TEXT_INPUT:
                        bytes = recvint(read_fd);
                        read(read_fd, input_buffer, bytes);
                        input_buffer[bytes] = '\0';

                        state_push_input(&state, input_buffer);
                        while ((status = state_handle_input(&state, &action, &key)) != STATE_NOTHING) {
                                switch (status) {
                                case STATE_ACTION_READY:
                                        vm_eval_function(&action, NULL);
                                        break;
                                case STATE_NOT_BOUND:
                                        if (state.mode == STATE_INSERT) {
                                                textbuffer_insert(&data, &cursor, key.str);
                                        }
                                        break;
                                }
                        }

                        adjust_scroll();
                        render();
                        break;
                case EVT_KEY_INPUT:
                        bytes = recvint(read_fd);
                        read(read_fd, input_buffer, bytes);
                        input_buffer[bytes] = '\0';

                        LOG("KEY = '%s'", input_buffer);

                        if (strcmp(input_buffer, "C-j") == 0) {
                                char *line = buffer_current_line();
                                sprintf(buf, "print(%s);", line);
                                textbuffer_insert(&data, &cursor, "\n");
                                if (strlen(line) == 0) goto done;
                                if (vm_execute(buf)) {
                                        textbuffer_insert(&data, &cursor, vm_get_output());
                                } else if (strstr(vm_error(), "ParseError") != NULL && (sprintf(buf, "%s\n", line), vm_execute(buf))) {
                                        textbuffer_insert(&data, &cursor, vm_get_output());
                                } else {
                                        textbuffer_insert(&data, &cursor, vm_error());
                                        textbuffer_insert(&data, &cursor, "\n");
                                }
                        } else {
                                state_push_input(&state, input_buffer);
                                while ((status = state_handle_input(&state, &action, &key)) != STATE_NOTHING) {
                                        switch (status) {
                                        case STATE_ACTION_READY:
                                                vm_eval_function(&action, NULL);
                                                break;
                                        case STATE_NOT_BOUND:
                                                if (state.mode == STATE_INSERT) {
                                                        textbuffer_insert(&data, &cursor, key.str);
                                                }
                                                break;
                                        }
                                }
                        }
done:
                        adjust_scroll();
                        render();
                        break;
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
                
                data = textbuffer_new();
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

        static char buf[READ_BUFFER_SIZE];
        struct location loc = { 0, 0 };
        bool ending_newline = false;
        ssize_t n;

        while (n = read(fd, buf, sizeof buf - 1), n > 0) {
                ending_newline = buf[n - 1] == '\n';
                buf[n] = '\0';
                textbuffer_insert(&data, &loc, buf);
        }
        // TODO: handle read errors

        if (ending_newline) {
                textbuffer_remove_line(&data, loc.line);
        }

        return BUFFER_LOAD_OK;
}

char *
buffer_current_line(void)
{
        return textbuffer_get_line(&data, cursor.line);
}

char *
buffer_get_line(int line)
{
        /*
         * This should be checked by the caller.
         */
        assert(line >= 0);

        if (line >= textbuffer_num_lines(&data)) {
                return NULL;
        } else {
                return textbuffer_get_line(&data, line);
        }
}

/*
 * Insert 'n' bytes from the data pointed to by 'text' into the buffer at the current cursor position.
 */
void
buffer_insert_n(char const *text, int n)
{
        textbuffer_insert_n(&data, &cursor, text, n);
}

int
buffer_remove(int n)
{
        return textbuffer_remove(&data, cursor, n);
}

int
buffer_forward(int n)
{
        return textbuffer_move_forward(&data, &cursor, n);
}

int
buffer_backward(int n)
{
        return textbuffer_move_backward(&data, &cursor, n);
}

int
buffer_right(int n)
{
        return textbuffer_move_right(&data, &cursor, n);
}

int
buffer_left(int n)
{
        return textbuffer_move_left(&data, &cursor, n);
}

int
buffer_line(void)
{
        return cursor.line;
}

int
buffer_column(void)
{
        return cursor.col;
}

int
buffer_lines(void)
{
        return textbuffer_num_lines(&data);
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
        return textbuffer_next_line(&data, &cursor, amount);
}

int
buffer_prev_line(int amount)
{
        return textbuffer_prev_line(&data, &cursor, amount);
}

int
buffer_scroll_down(int amount)
{
        // TODO: allow scrolling further than the last screen height

        int max_possible = (textbuffer_num_lines(&data) - lines) - scroll.line;
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
        textbuffer_cut_line(&data, cursor);
}

void
buffer_mark_values(void)
{
        state_mark_actions(&state);
}

TEST(setup)
{
        file = file_invalid();
        data = textbuffer_new();
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

        struct location loc = { 0, 238472 };
        textbuffer_insert(&data, &loc, "   testing   ");
}
