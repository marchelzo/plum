#ifndef COLORS_H_INCLUDED
#define COLORS_H_INCLUDED

bool
colors_init(void);

int
colors_next(int avoid);

void
colors_free(int);

#endif
