#ifndef ENVIRONMENT_H_INCLUDED
#define ENVIRONMENT_H_INCLUDED

#include "value.h"

struct environment;

struct environment *
env_new(struct environment *parent);

struct value *
env_lookup(struct environment *env, char const *id);

void
env_insert(struct environment *env, char const *id, struct value val);

void
env_update(struct environment *env, char const *id, struct value val);

void
env_update_or_insert_local(struct environment *env, char const *id, struct value val);

#endif
