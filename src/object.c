#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

#include "alloc.h"
#include "util.h"
#include "value.h"
#include "object.h"
#include "log.h"
#include "gc.h"

enum {
        OBJECT_NUM_BUCKETS = 16
};

struct object_hashmap_node {
        struct value key;
        struct value value;
        struct object_hashmap_node *next;
};

struct object {
        struct object_hashmap_node *buckets[OBJECT_NUM_BUCKETS];

        bool mark;
        struct object *next;
};

static struct value nil = { .type = VALUE_NIL };

static struct object *object_chain = NULL;

static struct object_hashmap_node *
mknode(struct value key, struct value value, struct object_hashmap_node *next)
{
        struct object_hashmap_node *node = gc_alloc(sizeof *node);

        node->key = key;
        node->value = value;
        node->next = next;

        return node;
}

static void
freeobj(struct object *obj)
{
        for (int i = 0; i < OBJECT_NUM_BUCKETS; ++i) {
                for (struct object_hashmap_node *node = obj->buckets[i]; node != NULL;) {
                        struct object_hashmap_node *next = node->next;
                        LOG("FREEING OBJECT NODE");
                        free(node);
                        node = next;
                }
        }

        LOG("FREEING OBJECT");

        free(obj);
}

static struct value *
bucket_find(struct object_hashmap_node *node, struct value const *key)
{
        while (node != NULL) {
                if (value_test_equality(&node->key, key)) {
                        return &node->value;
                } else {
                        node = node->next;
                }
        }

        return NULL;
}

struct object *
object_new(void)
{
        struct object *object = gc_alloc(sizeof *object);

        for (int i = 0; i < OBJECT_NUM_BUCKETS; ++i) {
                object->buckets[i] = NULL;
        }

        object->mark = true;
        object->next = object_chain;
        object_chain = object;

        return object;
}

struct value *
object_get_value(struct object const *obj, struct value const *key)
{
        unsigned bucket_index = value_hash(key) % OBJECT_NUM_BUCKETS;
        return bucket_find(obj->buckets[bucket_index], key);
}

void
object_put_value(struct object *obj, struct value key, struct value value)
{
        unsigned bucket_index = value_hash(&key) % OBJECT_NUM_BUCKETS;
        struct value *valueptr = bucket_find(obj->buckets[bucket_index], &key);
        
        if (valueptr == NULL) {
                obj->buckets[bucket_index] = mknode(key, value, obj->buckets[bucket_index]);
        } else {
                *valueptr = value;
        }
}

struct value *
object_put_key_if_not_exists(struct object *obj, struct value key)
{
        unsigned bucket_index = value_hash(&key) % OBJECT_NUM_BUCKETS;
        struct value *valueptr = bucket_find(obj->buckets[bucket_index], &key);

        if (valueptr != NULL) {
                return valueptr;
        } else {
                obj->buckets[bucket_index] = mknode(key, nil, obj->buckets[bucket_index]);
                return &obj->buckets[bucket_index]->value;
        }
}

struct value *
object_put_member_if_not_exists(struct object *obj, char const *member)
{
        return object_put_key_if_not_exists(obj, (struct value){ .type = VALUE_STRING, .string = member });
}

struct value *
object_get_member(struct object const *obj, char const *key)
{
        return object_get_value(
                obj,
                &(struct value){ .type = VALUE_STRING, .string = key }
        );
}

void
object_put_member(struct object *obj, char const *key, struct value value)
{
        object_put_value(
                obj,
                (struct value){ .type = VALUE_STRING, .string = key }, // TODO: maybe clone the key
                value
        );
}

struct value
object_keys_array(struct object *obj)
{
        struct value keys;
        keys.type = VALUE_ARRAY;
        keys.array = value_array_new();

        for (int i = 0; i < OBJECT_NUM_BUCKETS; ++i) {
                for (struct object_hashmap_node *node = obj->buckets[i]; node != NULL; node = node->next) {
                        vec_push(*keys.array, node->key);
                }
        }

        return keys;
}

void
object_mark(struct object *obj)
{
        obj->mark = true;

        for (int i = 0; i < OBJECT_NUM_BUCKETS; ++i) {
                for (struct object_hashmap_node *node = obj->buckets[i]; node != NULL; node = node->next) {
                        LOG("MARKING KEY-VALUE PAIR");
                        LOG("(%s, %s)", value_show(&node->key), value_show(&node->value));
                        value_mark(&node->key);
                        value_mark(&node->value);
                }
        }
}

void
object_sweep(void)
{
        while (object_chain != NULL && !object_chain->mark) {
                struct object *next = object_chain->next;
                freeobj(object_chain);
                object_chain = next;
        }
        if (object_chain != NULL) {
                object_chain->mark = false;
        }
        for (struct object *obj = object_chain; obj != NULL && obj->next != NULL;) {
                struct object *next;
                if (!obj->next->mark) {
                        next = obj->next->next;
                        freeobj(obj->next);
                        obj->next = next;
                } else {
                        next = obj->next;
                }
                if (next != NULL) {
                        next->mark = false;
                }
                obj = next;
        }
}

void
object_gc_reset(void)
{
        object_chain = NULL;
}
