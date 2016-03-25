#ifndef STATE_H_INCLUDED
#define STATE_H_INCLUDED

#include "vec.h"
#include "value.h"

enum event_type {

        EVENT_WRITE_PRE       = 1 << 0,
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

enum {
        STATE_ACTION_READY,
        STATE_NOT_BOUND,
        STATE_NOTHING,

        KEY_CHORD_TIMEOUT_MS = 300,
};

struct input_state;
struct input_transition;

struct input_state {
        vec(struct input_transition) transitions;
        bool has_action;
        struct value f;
};

struct input_transition {
        struct input_state *s;
        char key[16];
};


struct event_handler {
        int on;
        struct value f;
};

struct key {
        char str[16];
};

/*
 * okay
 */
struct state {
        vec(struct event_handler) event_handlers;

        /*
         * Start states of each FSM.
         */
        struct input_state *normal_start;
        struct input_state *insert_start;
        struct input_state *txtobj_start;

        struct input_state *current_state;

        int index;

        struct value *action;
        int action_index;

        long ms;

        vec(struct key) input_buffer;

        enum { STATE_NORMAL, STATE_INSERT } mode;
};

int
state_handle_input(struct state *s, struct value *f, struct key *kp);

void
state_push_input(struct state *s, char const *key);

struct state
state_new(void);

void
state_map_normal(struct state *s, struct value_array *keys, struct value f);

void
state_map_insert(struct state *s, struct value_array *keys, struct value f);

void
state_enter_insert(struct state *s);

void
state_enter_normal(struct state *s);

void
state_mark_actions(struct state *s);

#endif
