#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>

#include "file.h"
#include "panic.h"
#include "util.h"

struct file
file_invalid(void)
{
        return (struct file) {
                .path = NULL
        };
}

struct file
file_new(char const *path)
{
        struct stat fileinfo;
        errno = 0;
        if (stat(path, &fileinfo) != 0 && errno != ENOENT) {
                panic("call to stat(2) failed!");
        }

        bool isnew = errno == ENOENT;

        errno = 0;
        if (access(path, W_OK) != 0 && errno != EACCES) {
                panic("call to access(2) failed!");
        }

        bool canwrite = errno != EACCES;

        return (struct file) {
                .path     = sclone(path),
                .isnew    = isnew,
                .canwrite = canwrite
        };
}

bool
file_is_valid(struct file const *f)
{
        return f->path != NULL;
}
