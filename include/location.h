#ifndef LOCATION_H_INCLUDED
#define LOCATION_H_INCLUDED

#include <stdbool.h>

struct location {
        int line;
        int col;
};

inline static bool
location_is_origin(struct location loc)
{
        return (loc.line == 0) && (loc.col == 0);
}

inline static bool
location_same(struct location l1, struct location l2)
{
        /*
         * Compare the columns first because they're more likely to
         * be different (lmfao).
         */
        return (l1.col == l2.col) && (l1.line == l2.line);
}

#endif
