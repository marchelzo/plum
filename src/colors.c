#include <math.h>
#include <limits.h>
#include <stdbool.h>
#include <curses.h>

static struct {
        unsigned color;
        int used;
} colors[] = {
        { 0xF9F9F9, 0       },
        { 0xFCFDCD, 0       },
        { 0xFDE6F6, 0       },
        { 0xCEF4FC, 0       },
        { 0xFCCECE, 0       },
        { 0xF6F5EE, 0       },
        { 0xBBF7BA, 0       },
        { 0xFFF3CC, 0       },
        { 0xD6DDFF, 0       },
        { 0x0DEAD0, INT_MAX },
};

static int const nc = (sizeof colors / sizeof colors[0]) - 1;

#define R(c) (((c) >> 16) & 0xFF)
#define G(c) (((c) >>  8) & 0xFF)
#define B(c) (((c) >>  0) & 0xFF)
inline static void
mkcolor(int n, unsigned color)
{
        float scale = 1000.0 / 256.0;
        unsigned short r = round(R(color) * scale);
        unsigned short g = round(G(color) * scale);
        unsigned short b = round(B(color) * scale);
        init_color(n, r, g, b);
}

bool
colors_init(void)
{
        if (!has_colors() || !can_change_color())
                return false;

        start_color();

        init_color(0, 150, 150, 150);

        for (int i = 0; i < nc; ++i) {
                mkcolor(i + 1, colors[i].color);
                init_pair(i + 1, 0, i + 1);
        }

        return true;
}

void
colors_free(int c)
{
        --colors[c - 1].used;
}

int
colors_next(int avoid)
{
        int c = nc;

        for (int i = 0; i < nc; ++i)
                if (colors[i].used < colors[c].used && i != avoid)
                        c = i;

        ++colors[c].used;

        return c + 1;
}
