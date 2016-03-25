#ifndef STATE_H_INCLUDED
#define STATE_H_INCLUDED

#include "vec.h"
#include "value.h"

enum event_type {

        EVENT_WRITE_PRE       = 1 << 0
        EVENT_WRITE_POST      = 1 << 1,

        EVENT_OPEN_PRE        = 1 << 2,
        EVENT_OPEN_POST       = 1 << 3,

        EVENT_ENTER_PRE       = 1 << 4,
        EVENT_ENTER_POST      = 1 << 5,

        EVENT_LEAVE_PRE       = 1 << 6,
        EVENT_LEAVE_POST      = 1 << 7,

        EVENT_CLOSE_PRE       = 1 << 8,
        EVENT_CLOSE_POST      = 1 << 9,

        EVENT_BACKGROUND_PRE  = 1 << 10,
        EVENT_BACKGRONUD_POST = 1 << 11,

};

struct binding {
        vec(char *) keys;
        struct value f;
};

/*
 * Maybe these are a bad idea, and everything should be done through bindings alone.
 */
struct mapping {
        vec(char *) to;
        vec(char *) from;
};

struct event_handler {
        int on;
        struct value f;
};

/*
 * What is a mode, anyway?
 *
 * Modes consist of three things:
 *
 *      - Bindings
 *      - Mappings
 *      - Event handlers
 *
 * Bindings _bind_ key-chords to code. For example, "C-s" might be _bound_ to
 * the function buffer::writeFile.
 *
 * Mappings _map_ key-chords to other key-chords. For example, "C-S-s" might
 * be _mapped_ to "C-s".
 *
 * Event handlers... well... handle events. A mode may have an onWrite handler
 * that runs a build script, or an onBackground handler that runs some code when a
 * the buffer is backgrounded.
 *
 */
struct mode {
        
        vec(struct event_handler) event_handlers;

        vec(struct binding) normal_bindings;
        vec(struct mapping) normal_mappings;

        vec(struct binding) insert_bindings;
        vec(struct mapping) insert_mappings;

};

#endif
