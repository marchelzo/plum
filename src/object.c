#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "alloc.h"
#include "util.h"
#include "value.h"
#include "object.h"

enum {
        OBJECT_NUM_BUCKETS = 16
};

struct object_hashmap_node {
        struct value key;
        struct value value;
        struct object_hashmap_node *next;
};

struct object {
        struct object_hashmap_node **buckets;
};

static struct object_hashmap_node *
mknode(struct value key, struct value value, struct object_hashmap_node *next)
{
        struct object_hashmap_node *node = alloc(sizeof *node);

        node->key = key;
        node->value = value;
        node->next = next;

        return node;
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
        struct object_hashmap_node **buckets = alloc(OBJECT_NUM_BUCKETS * sizeof (struct object_hashmap_node *));
        for (int i = 0; i < OBJECT_NUM_BUCKETS; ++i) {
                buckets[i] = NULL;
        }

        struct object *object = alloc(sizeof *object);
        object->buckets = buckets;

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
