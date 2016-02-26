#include <stdlib.h>

#include "alloc.h"
#include "panic.h"

#define vec(T) \
        struct { \
                T *items; \
                size_t count; \
                size_t capacity; \
        }

#define vec_get(v, idx) \
        ((v).items + idx)

#define vec_push(v, item) \
          (((v).count >= (v).capacity) \
        ? ((resize((v).items, ((v).capacity = ((v).capacity == 0 ? 4 : ((v).capacity * 2))) * (sizeof (*(v).items)))), \
                        ((v).items[(v).count++] = (item)), \
                        ((v).items + (v).count - 1)) \
        : (((v).items[(v).count++] = (item)), \
                ((v).items + (v).count - 1)))

#define vec_pop(v) \
    ((v).count == 0 ? NULL : (v).items + --(v).count)

#define vec_init(v) \
    (((v).capacity = 0), ((v).count = 0), ((v).items = NULL))

#define vec_empty(v) \
    ((v.capacity = 0), (v.count = 0), free(v.items), (v.items = NULL))

#define vec_len(v) ((v).count)

#define vec_for_each(v, idx, name) for (size_t idx = 0; ((name) = vec_get((v), idx)), idx < (v).count; ++idx)
