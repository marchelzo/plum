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

#endif
