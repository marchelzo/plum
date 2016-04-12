#ifndef STATE_H_INCLUDED
#define STATE_H_INCLUDED

#include "vec.h"
#include "value.h"
#include "object.h"

enum event_type {

        EVENT_WRITE_PRE,
        EVENT_WRITE_POST,

        EVENT_OPEN_PRE,
        EVENT_OPEN_POST,

        EVENT_ENTER_PRE,
        EVENT_ENTER_POST,

        EVENT_LEAVE_PRE,
        EVENT_LEAVE_POST,

        EVENT_CLOSE_PRE,
        EVENT_CLOSE_POST,

        EVENT_BACKGROUND_PRE,
        EVENT_BACKGRONUD_POST,

        NUM_EVENTS
        
};

enum {
        STATE_ACTION_READY,
        STATE_NOT_BOUND,
        STATE_NOTHING,
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

        vec(struct value) event_handlers[NUM_EVENTS];

        struct object *message_handlers;

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

        bool pending;

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

inline static bool
state_pending_input(struct state *s)
{
        return s->current_state->has_action || (s->mode == STATE_INSERT && s->current_state != s->insert_start);
}

void
state_register_message_handler(struct state *s, struct value type, struct value f);

void
state_handle_message(struct state *s, struct value bufid, struct value type, struct value msg);

#endif
