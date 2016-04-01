#ifndef SUBPROCESS_H_INCLUDED
#define SUBPROCESS_H_INCLUDED

/* spawn a new subprocess */
bool
sp_tryspawn(char *path, struct value_array *args, struct value on_stdout, struct value on_exit, int fds[static 2]);

void
sp_on_exit(int fd);

void
sp_on_output(int fd, char const *data, int n);

/*
 * find out if 'fd' is a valid file descriptor for a currently-running
 * subprocess's stdin.
 */
bool
sp_fdvalid(int fd);

void
sp_kill(int fd);

void
sp_close(int fd);

#endif
