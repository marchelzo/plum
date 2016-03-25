#ifndef GC_H_INCLUDED
#define GC_H_INCLUDED

extern int gc_prevent;

void *
gc_alloc(size_t n);

void
gc_reset(void);

#endif
