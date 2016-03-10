#include <string.h>

#include "value.h"
#include "object.h"
#include "vm.h"
#include "log.h"

enum {
        GC_ALLOC_THRESHOLD = (1 << 24) // ~ 16 MB
};

static size_t allocated = 0;

void *
gc_alloc(size_t n)
{
        void *mem = alloc(n);

        allocated += n;

        if (allocated <= GC_ALLOC_THRESHOLD) {
                return mem;
        }

        vm_mark();        

        object_sweep();
        value_array_sweep();
        value_ref_vector_sweep();
        vm_sweep_variables();

        allocated = 0;

        return mem;
}

void
gc_reset(void)
{
        allocated = 0;
        value_gc_reset();
        object_gc_reset();
}
