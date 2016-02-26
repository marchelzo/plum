#ifndef OBJECT_H_INCLUDED
#define OBJECT_H_INCLUDED

#include "value.h"

struct object;

struct object *
object_new(void);

struct value *
object_get_value(struct object const *obj, struct value const *key);

void
object_put_value(struct object *obj, struct value key, struct value value);

struct value *
object_get_member(struct object const *obj, char const *key);

void
object_put_member(struct object *obj, char const *key, struct value value);

#endif
