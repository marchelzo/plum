#include "state.h"
#include "location.h"
#include "utf8.h"
#include "tb.h"
#include "log.h"

#define FORWARD 1
#define BACKWARD 2

#define RIGHT(s) ((s)->right + (s)->capacity - (s)->rightcount)

static struct stringpos out;
static struct stringpos lim = { -1, -1, -1, -1, -1 };

inline static void
stringcount(char const *s, int bytes)
{
        lim.bytes = bytes;
        utf8_stringcount(s, bytes, &out, &lim);
}

static char
findpair(char c, unsigned char *direction)
{
        static char const pairs[][2] = {
                { ')', '(' },
                { ']', '[' },
                { '}', '{' },
        };

        static int const np = sizeof pairs / 2;

        for (int i = 0; i < np; ++i) {
                if (pairs[i][0] == c) {
                        *direction = BACKWARD;
                        return pairs[i][1];
                } else if (pairs[i][1] == c) {
                        *direction = FORWARD;
                        return pairs[i][0];
                }
        }

        return 0;
}

inline static char const *
bkwdmatch(char const *s, char c, char p, int n)
{
        int d = 0;

        while (n --> 0) {
                if (s[n] == c)
                        ++d;
                else if (s[n] == p && d-- == 0)
                        return s + n;
        }

        return NULL;
}

inline static char const *
fwdmatch(char const *s, char c, char p, int n)
{
        int d = 0;

        for (int i = 0; i < n; ++i) {
                if (s[i] == c)
                        ++d;
                else if (s[i] == p && d-- == 0)
                        return s + i;
        }

        return NULL;
}

struct location
matchpair(struct tb const *s, char const *start, char const *end, int line, int col)
{
        struct location result = { -1, -1 };

        char p;
        unsigned char d;
        char const *m = NULL;
        char const *r = RIGHT(s);
        int nl = s->left + s->leftcount - start;

        if (s->rightcount > 0 && (p = findpair(r[0], &d)) != 0) {
                switch (d) {
                case FORWARD:  m = fwdmatch(r + 1, r[0], p, end - r); break;
                case BACKWARD: m = bkwdmatch(start, r[0], p, nl);     break;
                }
        }

        if (m == NULL) {
                if (buffer_mode() == STATE_INSERT)
                        goto Left;
                else
                        goto End;
        }

Result:
        switch (d) {
        case FORWARD:
                stringcount(r, m - r);
                result.line = out.lines + line;
                result.col = out.column + (out.lines == 0 ? col : 0);
                break;
        case BACKWARD:
                stringcount(start, m - start);
                result.line = out.lines;
                result.col = out.column;
                break;
        }


        goto End;
Left:

        if (s->leftcount == 0)
                goto End;

        char c = s->left[s->leftcount - 1];
        if (s->leftcount > 0 && (p = findpair(c, &d)) != 0) {
                switch (d) {
                case FORWARD:  m = fwdmatch(r, c, p, end - r + 1); break;
                case BACKWARD: m = bkwdmatch(start, c, p, nl - 1); break;
                }
        }

        if (m != NULL)
                goto Result;

End:
        return result;
}
