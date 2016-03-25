#include <string.h>
#include <sys/time.h>

#include "log.h"
#include "value.h"
#include "alloc.h"
#include "state.h"

#define STARTSTATE(s) ((s)->mode == STATE_NORMAL ? (s)->normal_start : (s)->insert_start)

inline static void
remove_input(struct state *s, int n)
{
        memmove(
                s->input_buffer.items,
                s->input_buffer.items + n,
                (s->input_buffer.count - n) * sizeof (struct key)
        );

        s->input_buffer.count -= n;
}

inline static void
pushkey(struct state *s, char const *key)
{
        struct key k;
        strcpy(k.str, key);
        vec_push(s->input_buffer, k);
}

inline static struct input_state *
mkstate(void)
{
        struct input_state *s = alloc(sizeof *s);
        s->has_action = false;
        vec_init(s->transitions);

        return s;
}

inline static void
markall(struct input_state *s)
{
        if (s == NULL) {
                return;
        }

        if (s->has_action) {
                value_mark(&s->f);
        }

        int n = s->transitions.count;
        for (int i = 0; i < n; ++i) {
                markall(s->transitions.items[i].s);
        }
}

inline static struct input_state *
findnext(struct input_state *s, char const *key, int bytes)
{
        LOG("Looking for: %s", key);
        int n = s->transitions.count;
        for (int i = 0; i < n; ++i) {
                struct input_transition *t = &s->transitions.items[i];
                LOG("Comparing against: %s", t->key);
                int klen = strlen(t->key);
                if (klen == bytes && strncmp(key, t->key, bytes) == 0) {
                        LOG("Found it!");
                        return t->s;
                }
        }

        LOG("Could't find it!");
        return NULL;
}

inline static void
domap(struct input_state *state, struct value_array *keys, struct value f)
{
        struct input_state *next;

        LOG("MAPPING:");
        for (int i = 0; i < keys->count; ++i) {
                LOG("Key %d: %s", i + 1, keys->items[i].string);
        }

        int i = 0;
        while (i < keys->count && (next = findnext(state, keys->items[i].string, keys->items[i].bytes))) {
                ++i;
                state = next;
        }

        while (i < keys->count) {
                struct input_transition t;
                LOG("Making a new transition: %d bytes: %s", keys->items[i].bytes, keys->items[i].string);
                memcpy(t.key, keys->items[i].string, keys->items[i].bytes);
                t.key[keys->items[i].bytes] = '\0';
                t.s = mkstate();
                vec_push(state->transitions, t);

                state = t.s;
                ++i;
        }

        state->has_action = true;
        state->f = f;
}

struct state
state_new(void)
{
        struct state state;

        vec_init(state.event_handlers);
        vec_init(state.input_buffer);

        state.action = NULL;
        state.action_index = 0;
        state.index = 0;
        state.ms = 0;

        state.normal_start = mkstate();
        state.insert_start = mkstate();
        state.txtobj_start = mkstate();

        /*
         * Start in normal mode.
         */
        state.current_state = state.normal_start;
        state.mode = STATE_NORMAL;

        return state;
}

void
state_push_input(struct state *s, char const *key)
{
        pushkey(s, key);
}

void
state_enter_normal(struct state *s)
{
        s->current_state = s->normal_start;
        s->mode = STATE_NORMAL;
}

void
state_enter_insert(struct state *s)
{
        s->current_state = s->insert_start;
        s->mode = STATE_INSERT;
}

void
state_map_normal(struct state *s, struct value_array *keys, struct value f)
{
        domap(s->normal_start, keys, f);
}

void
state_map_insert(struct state *s, struct value_array *keys, struct value f)
{
        domap(s->insert_start, keys, f);
}

/*
 * The fact that this is even _somewhat_ correct is a miracle.
 * Easily the ugliest part of the project.
 */
int
state_handle_input(struct state *s, struct value *f, struct key *kp)
{

        if (s->input_buffer.count == 0) {
                return STATE_NOTHING;
        }

        struct timeval t;
        gettimeofday(&t, NULL);

        long ms = t.tv_sec * 1000 + t.tv_usec / 1000;
        long dt = ms - s->ms;

        if (s->index == s->input_buffer.count) {
                if (dt >= KEY_CHORD_TIMEOUT_MS && (s->current_state->has_action || s->mode == STATE_INSERT)) {
                        s->ms = ms;
                        if (s->action != NULL) {
                                *f = *s->action;
                                remove_input(s, s->action_index);
                                s->action_index = -1;
                                s->index = 0;
                                s->action = NULL;
                                s->current_state = STARTSTATE(s);
                                return STATE_ACTION_READY;
                        } else {
                                *kp = s->input_buffer.items[0];
                                remove_input(s, 1);
                                s->index = 0;
                                s->current_state = STARTSTATE(s);
                                return STATE_NOT_BOUND;
                        }

                }

                return STATE_NOTHING;
        }

        s->ms = ms;

        char const *key = s->input_buffer.items[s->index].str;
        ++s->index;

        struct input_state *next = findnext(s->current_state, key, strlen(key));
        if (next != NULL) {
                s->current_state = next;
                if (s->current_state->has_action) {
                        s->action = &s->current_state->f;
                        s->action_index = s->index;
                }
                if (s->current_state->transitions.count == 0) {
                        *f = *s->action;
                        remove_input(s, s->action_index);
                        s->action_index = -1;
                        s->index = 0;
                        s->action = NULL;
                        s->current_state = STARTSTATE(s);
                        return STATE_ACTION_READY;
                }
        }

        if (s->current_state == (STARTSTATE(s)) || next == NULL) {
                *kp = s->input_buffer.items[0];
                remove_input(s, 1);
                s->index = 0;
                s->current_state = STARTSTATE(s);
                return STATE_NOT_BOUND;
        }
        
        return STATE_NOTHING;
}

void
state_mark_actions(struct state *s)
{
        markall(s->normal_start);
        markall(s->insert_start);
}
