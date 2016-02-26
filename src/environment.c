#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "environment.h"
#include "value.h"
#include "test.h"

struct environment {
        struct environment *parent;
        size_t size;
        size_t allocated;
        const char **keys;
        struct value *values;
};

inline static void
grow_env(struct environment *env)
{
        env->allocated += 4;

        resize(env->keys, sizeof *env->keys * env->allocated);
        resize(env->values, sizeof *env->values * env->allocated);
}

struct environment *
env_new(struct environment *parent)
{
        struct environment *env = alloc(sizeof *env);
        env->parent = parent;
        env->size = 0;
        env->allocated = 0;
        env->keys = NULL;
        env->values = NULL;
        return env;
}

struct value *
env_lookup(struct environment *env, char const *id)
{
        if (env == NULL) {
                return NULL;
        }

        for (size_t i = 0; i < env->size; ++i) {
                if (strcmp(env->keys[i], id) == 0) {
                        return &env->values[i];
                }
        }

        return env_lookup(env->parent, id);
}

void env_insert(struct environment *env, const char *id, struct value val)
{
        if (env->size == env->allocated) {
                grow_env(env);
        }

        env->keys[env->size] = id;
        env->values[env->size] = val;

        env->size += 1;
}

void env_update(struct environment *env, const char *id, struct value val)
{
        while (env != NULL) {
                for (size_t i = 0; i < env->size; ++i) {
                        if (strcmp(env->keys[i], id) == 0) {
                                env->values[i] = val;
                                return;
                        }
                }
                env = env->parent;
        }
}

void env_update_or_insert_local(struct environment *env, const char *id, struct value val)
{
        for (size_t i = 0; i < env->size; ++i) {
                if (strcmp(env->keys[i], id) == 0) {
                        env->values[i] = val;
                        return;
                }
        }

        env_insert(env, id, val);
}

TEST(env)
{
        struct environment *env = env_new(NULL);

        struct value hello = { .type = VALUE_STRING, .string = "hello" };

        env_insert(env, "h", hello);

        struct value *h = env_lookup(env, "h");

        claim(h != NULL);
        claim(h->type == VALUE_STRING);
        claim(strcmp(h->string, "hello") == 0);
}
