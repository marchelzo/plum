#ifndef PROTOCOL_H_INCLUDED
#define PROTOCOL_H_INCLUDED

#include <string.h>
#include <unistd.h>
#include <errno.h>

#include "panic.h"

#define buffer_event_code char

enum {
        EVT_CHILD_LOCKED_MUTEX,
        EVT_LOAD_FILE,
        EVT_START_CONSOLE,
        EVT_WINDOW_DIMENSIONS,
        EVT_PARENT_SYNCED_BUFFER,
        EVT_TEXT_INPUT,
        EVT_KEY_INPUT,
        EVT_GROW_X_REQUEST,
        EVT_GROW_Y_REQUEST,
        EVT_HSPLIT_REQUEST,
        EVT_VSPLIT_REQUEST,
        EVT_NEXT_WINDOW_REQUEST,
        EVT_PREV_WINDOW_REQUEST,
        EVT_VM_ERROR,
        EVT_LOG_REQUEST,
        EVT_SHOW_CONSOLE_REQUEST,
        EVT_WINDOW_ID,
        EVT_WINDOW_DELETE_REQUEST,
        EVT_WINDOW_DELETE_CURRENT_REQUEST
};

static inline void
evt_send(int fd, buffer_event_code code)
{
        write(fd, &code, sizeof code);
}

static inline buffer_event_code
evt_recv(int fd)
{
        buffer_event_code code;

        if (read(fd, &code, sizeof code) < 0) {
                panic("read() failed: %s", strerror(errno));
        }

        return code;
}

static inline void
sendint(int fd, int val)
{
        write(fd, &val, sizeof (int));
}

static inline int
recvint(int fd)
{
        int val;
        read(fd, &val, sizeof (int));
        return val;
}

#endif
