#ifndef FILE_H_INCLUDED
#define FILE_H_INCLUDED

#include <stdbool.h>

struct file {
        char *path;
        bool isnew;
        bool canwrite;
};

struct file
file_invalid(void);

struct file
file_new(char const *path);

#endif
