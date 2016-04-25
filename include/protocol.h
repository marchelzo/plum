#ifndef PROTOCOL_H_INCLUDED
#define PROTOCOL_H_INCLUDED

#include <string.h>
#include <unistd.h>
#include <errno.h>

#include "panic.h"
#include "log.h"

#define buffer_event_code char

enum {
        EVT_CHILD_LOCKED_MUTEX,
        EVT_LOAD_FILE,
        EVT_START_CONSOLE,
        EVT_WINDOW_DIMENSIONS,
        EVT_PARENT_SYNCED_BUFFER,
        EVT_TEXT_INPUT,
        EVT_KEY_INPUT,
        EVT_MESSAGE,
        EVT_BACKGROUNDED,
        EVT_NEW_BUFFER,
        EVT_RUN_PROGRAM,
        EVT_GROW_X,
        EVT_GROW_Y,
        EVT_HSPLIT,
        EVT_VSPLIT,
        EVT_NEXT_WINDOW,
        EVT_PREV_WINDOW,
        EVT_GOTO_WINDOW,
        EVT_WINDOW_RIGHT,
        EVT_WINDOW_LEFT,
        EVT_WINDOW_DOWN,
        EVT_WINDOW_UP,
        EVT_VM_ERROR,
        EVT_LOG,
        EVT_SHOW_CONSOLE,
        EVT_STATUS_MESSAGE,
        EVT_WINDOW_ID,
        EVT_WINDOW_DELETE,
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
