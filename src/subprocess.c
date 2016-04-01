#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

#include <signal.h>
#include <fcntl.h>
#include <unistd.h>

#include "util.h"
#include "alloc.h"
#include "value.h"
#include "subprocess.h"
#include "log.h"
#include "vm.h"

struct sp {
        pid_t pid;
        int input;
        int output;
        struct value on_output;
        struct value on_exit;
        char *path;
};

static vec(struct sp) jobs;

/* find a job given the fd for the read-end of its pipe */
inline static int
outfind(int fd)
{
        for (int i = 0; i < jobs.count; ++i)
                if (jobs.items[i].output == fd)
                        return i;
        return -1;
}

/* find a job given the fd for the write-end of its pipe */
inline static int
infind(int fd)
{
        for (int i = 0; i < jobs.count; ++i)
                if (jobs.items[i].input == fd)
                        return i;
        return -1;
}

inline static void
deljob(int i)
{
        close(jobs.items[i].input);
        close(jobs.items[i].output);
        free(jobs.items[i].path);

        struct value f = jobs.items[i].on_exit;

        jobs.items[i] = *vec_last(jobs);
        --jobs.count;

        if (f.type != VALUE_NIL)
                vm_eval_function(&f, &NIL);
}

bool
sp_tryspawn(char *path, struct value_array *args, struct value on_output, struct value on_exit, int fds[static 2])
{
        pid_t pid;

        int c2p[2];
        int p2c[2];
        int exc[2];

        if (pipe(c2p) != 0) {
                return false;
        }
        if (pipe(p2c) != 0) {
                close(c2p[0]);
                close(c2p[1]);
                return false;
        }
        if (pipe(exc) != 0) {
                close(c2p[0]);
                close(c2p[1]);
                close(p2c[0]);
                close(p2c[1]);
                return false;
        }

        if (pid = fork(), pid == -1)
                return false;

        if (pid == 0) {
                // child
                close(c2p[0]);
                close(p2c[1]);
                close(exc[0]);
                if (dup2(p2c[0], STDIN_FILENO) == -1 || dup2(c2p[1], STDOUT_FILENO) == -1) {
                        write(exc[1], &errno, sizeof errno);
                        exit(EXIT_FAILURE);
                }

                /* close the exc pipe after we exec, so the parent knows everything worked */
                fcntl(exc[1], F_SETFD, FD_CLOEXEC);

                char *argv[17] = { path };
                for (int i = 0; i < 16 && i < args->count; ++i) {
                        argv[i + 1] = alloc(args->items[i].bytes + 1);
                        memcpy(argv[i + 1], args->items[i].string, args->items[i].bytes);
                        argv[i + 1][args->items[i].bytes] = '\0';
                }
                argv[min(16, args->count + 1)] = NULL;

                /* make stdio line-buffered */
                setlinebuf(stdin);
                setlinebuf(stdout);

                if (execvp(path, argv) == -1) {
                        write(exc[1], &errno, sizeof errno);
                        exit(EXIT_FAILURE);
                }

        } else {
                // parent
                close(c2p[1]);
                close(p2c[0]);
                close(exc[1]);

                // make sure the process was successfully started
                int status;
                if (read(exc[0], &status, sizeof status) != 0) {
                        errno = status;
                        close(c2p[0]);
                        close(p2c[1]);
                        close(exc[0]);
                        return false;
                }

                close(exc[0]);

                // everything went well. add the process to the list.
                struct sp sp = {
                        .pid = pid,
                        .input = p2c[1],
                        .output = c2p[0],
                        .on_output = on_output,
                        .on_exit = on_exit,
                        .path = path
                };

                vec_push(jobs, sp);

                fds[0] = sp.input;
                fds[1] = sp.output;

                return true;
        }
}

bool
sp_fdvalid(int fd)
{
        return infind(fd) != -1;
}

void
sp_on_exit(int fd)
{
        int i = outfind(fd);
        deljob(i);
}

void
sp_on_output(int fd, char const *data, int n)
{
        int i = outfind(fd);
        if (jobs.items[i].on_output.type == VALUE_NIL)
                return;

        struct value string = STRING_CLONE(data, n);
        vm_eval_function(&jobs.items[i].on_output, &string);
}

void
sp_kill(int fd)
{
        int i = infind(fd);
        kill(jobs.items[i].pid, SIGTERM);
}

void
sp_close(int fd)
{
        if (infind(fd) != -1)
                close(fd);
}
