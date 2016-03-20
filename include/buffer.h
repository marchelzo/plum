#ifndef BUFFER_H_INCLUDED
#define BUFFER_H_INCLUDED

#include <stdint.h>
#include <stdbool.h>

#include <unistd.h>
#include <pthread.h>

#include "file.h"
#include "textbuffer.h"

struct buffer {

        /*
         * true  - buffer process is using rb1 and parent should read from rb2
         * false - buffer process is using rb2 and parent should read from rb1
         */
        bool *rb_idx;
        bool *rb_changed;
        pthread_mutex_t *rb_mtx; // can't read/write rb_idx unless this is locked
        char *rb1;
        char *rb2;

        unsigned id;
        pid_t pid;

        int write_fd;
        int read_fd;
};

/*
 * This is called by the main editor process to spawn children.
 */
struct buffer
buffer_new(unsigned id);


/*
 * These functions are called within the child process and should never be called by the parent.
 */
void
buffer_insert_n(char const *text, int n);

#endif
