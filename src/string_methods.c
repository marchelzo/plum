#include <string.h>

#include <tickit.h>

#include "value.h"
#include "util.h"
#include "vm.h"

static TickitStringPos limitpos = { -1, -1, -1, -1 };
static TickitStringPos outpos;

inline static bool
is_prefix(char const *big, int blen, char const *small, int slen)
{
        return (blen >= slen) && (memcmp(big, small, slen) == 0);
}

inline static char const *
sfind(char const *big, int blen, char const *small, int slen)
{
        register int i;

        while (blen >= slen) {
                for (i = 0; i < slen; ++i) {
                        if (big[i] != small[i]) {
                                goto next;
                        }
                }

                return big;

next:
                ++big;
                --blen;
        }

        return NULL;

}

static struct value
string_length(struct value *string, value_vector *args)
{
        if (args->count != 0) {
                vm_panic("str.len() expects no arguments but got %zu", args->count);
        }

        limitpos.graphemes = -1;
        tickit_string_count(string->string, &outpos, &limitpos);

        return INTEGER(outpos.graphemes);
}

static struct value
string_slice(struct value *string, value_vector *args)
{
        if (args->count == 0 || args->count > 2) {
                vm_panic("str.slice() expects 1 or 2 arguments but got %zu", args->count);
        }

        struct value start = args->items[0];

        if (start.type != VALUE_INTEGER) {
                vm_panic("non-integer passed as first argument to str.slice()");
        }

        char const *s = string->string;
        int i = start.integer;
        int n;

        if (args->count == 2) {
                struct value len = args->items[1];
                if (len.type != VALUE_INTEGER) {
                        vm_panic("non-integer passed as second argument to str.slice()");
                }
                if (len.integer < 0) {
                        vm_panic("negative integer passed as second argument to str.slice()");
                }

                n = len.integer;
        } else {
                n = -1;
        }

        limitpos.graphemes = -1;
        limitpos.bytes = string->bytes;
        tickit_string_count(s, &outpos, &limitpos);

        if (i < 0) {
                i += outpos.graphemes;
        }
                
        if (i < 0 || i >= outpos.graphemes) {
                return NIL;
        }
        
        limitpos.graphemes = i;
        tickit_string_count(s, &outpos, &limitpos);

        s += outpos.bytes;
        limitpos.bytes -= outpos.bytes;

        limitpos.graphemes = n;
        tickit_string_count(s, &outpos, &limitpos);

        return STRINGN(s, outpos.bytes);
}

static struct value
string_search(struct value *string, value_vector *args)
{
        if (args->count != 1) {
                vm_panic("str.search() expects 1 argument but got %zu", args->count);
        }

        struct value pattern = args->items[0];

        if (pattern.type != VALUE_STRING && pattern.type != VALUE_REGEX) {
                vm_panic("the pattern argument to str.search() must be a string or a regex");
        }

        char const *s = string->string;
        int n;

        if (pattern.type == VALUE_STRING) {
                char const *match = memmem(s, string->bytes, pattern.string, pattern.bytes);

                if (match == NULL) {
                        return NIL;
                }

                n = match - s;
        } else {
                int len = string->bytes;
                pcre *re = pattern.regex;
                int rc;
                int out[3];

                rc = pcre_exec(re, NULL, s, len, 0, 0, out, 3);

                if (rc == -1) {
                        return NIL;
                }

                if (rc < -1) {
                        vm_panic("error executing regular expression");
                }

                n = out[0];
        }

        limitpos.graphemes = -1;
        limitpos.bytes = n;
        tickit_string_count(s, &outpos, &limitpos);
        limitpos.bytes = -1;

        return INTEGER(outpos.graphemes);
}

static struct value
string_split(struct value *string, value_vector *args)
{
        if (args->count != 1) {
                vm_panic("the split method on strings expects 1 argument but got %zu", args->count);
        }

        struct value pattern = args->items[0];

        if (pattern.type != VALUE_REGEX && pattern.type != VALUE_STRING) {
                vm_panic("invalid argument to the split method on string");
        }

        struct value result = ARRAY(value_array_new());

        char const *s = string->string;
        int len = string->bytes;
        if (pattern.type == VALUE_STRING) {
                char const *p = pattern.string;
                int n = pattern.bytes;

                if (n == 0) {
                        return result;
                }

                int i = 0;
                while (i < len && is_prefix(s + i, len - i, p, n)) {
                        i += n;
                }
                
                while (i < len) {

                        struct value str = STRINGN(s + i, 0);

                        while (i < len && !is_prefix(s + i, len - i, p, n)) {
                                ++str.bytes;
                                ++i;
                        }

                        vec_push(*result.array, str);

                        while (i < len && is_prefix(s + i, len - i, p, n)) {
                                i += n;
                        }
                }
        } else {
                pcre *re = pattern.regex;
                int len = string->bytes;
                int start = 0;
                int out[3];

                while (start < len) {
                        if (pcre_exec(re, NULL, s, len, start, 0, out, 3) != 1) {
                                out[0] = out[1] = len;
                        }

                        if (out[0] == out[1] && out[1] != len) {
                                ++out[0];
                                ++out[1];
                        }

                        int n = out[0] - start;
                        if (n == 0) {
                                goto next;
                        }

                        vec_push(*result.array, STRINGN(s + start, n));
next:
                        start = out[1];
                }
        }

        return result;
}

static struct value
string_replace(struct value *string, value_vector *args)
{
        static vec(char) chars = { .items = NULL, .count = 0, .capacity = 0 };

        if (args->count != 2) {
                vm_panic("the replace method on strings expects 2 arguments but got %zu", args->count);
        }

        struct value pattern = args->items[0];
        struct value replacement = args->items[1];

        if (pattern.type != VALUE_REGEX && pattern.type != VALUE_STRING) {
                vm_panic("the pattern argument to string's replace method must be a regex or a string");
        }

        if (replacement.type != VALUE_STRING && !CALLABLE(replacement)) {
                vm_panic("the replacement argument to string's replace method must be callable or a string");
        }

        chars.count = 0;

        char const *s = string->string;

        if (pattern.type == VALUE_STRING) {

                if (replacement.type != VALUE_STRING) {
                        vm_panic("non-string replacement passed to string's replace method with a string pattern");
                }

                char const *p = pattern.string;
                char const *r = replacement.string;

                int len = string->bytes;
                int plen = pattern.bytes;
                char const *m;

                while ((m = sfind(s, len, p, plen)) != NULL) {
                        vec_push_n(chars, s, m - s);

                        vec_push_n(chars, r, replacement.bytes);

                        len -= (m - s + plen);
                        s = m + plen;
                }

                vec_push_n(chars, s, len);
        } else if (replacement.type == VALUE_STRING) {
                pcre *re = pattern.regex;
                char const *r = replacement.string;
                int len = string->bytes;
                int start = 0;
                int out[3];

                while (pcre_exec(re, NULL, s, len, start, 0, out, 3) == 1) {

                        vec_push_n(chars, s + start, out[0] - start);

                        vec_push_n(chars, r, replacement.bytes);

                        start = out[1];
                }

                vec_push_n(chars, s + start, len - start);

        } else {
                pcre *re = pattern.regex;
                int len = string->bytes;
                int start = 0;
                int out[30];
                int rc;

                while ((rc = pcre_exec(re, NULL, s, len, start, 0, out, 30)) > 0) {

                        vec_push_n(chars, s + start, out[0] - start);

                        struct value match;

                        if (rc == 1) {
                                match = STRINGN(s + out[0], out[1] - out[0]);
                        } else {
                                match = ARRAY(value_array_new());

                                int j = 0;
                                for (int i = 0; i < rc; ++i, j += 2) {
                                        vec_push(*match.array, STRINGN(s + out[j], out[j + 1] - out[j]));
                                }
                        }

                        struct value repstr = vm_eval_function(&replacement, &match);
                        if (repstr.type != VALUE_STRING) {
                                vm_panic("non-string returned by the replacement function passed to string's replace method");
                        }

                        vec_push_n(chars, repstr.string, repstr.bytes);

                        start = out[1];
                }

                vec_push_n(chars, s + start, len - start);
        }

        return STRINGN(sclone(chars.items), chars.count);
}

static struct value
string_is_match(struct value *string, value_vector *args)
{
        if (args->count != 1) {
                vm_panic("the match? method on strings expects 1 argument but got %zu", args->count);
        }

        struct value pattern = args->items[0];

        if (pattern.type != VALUE_REGEX) {
                vm_panic("non-regex passed to the match? method on string");
        }

        int len = string->bytes;
        int rc;

        rc = pcre_exec(
                pattern.regex,
                NULL,
                string->string,
                len,
                0,
                0,
                NULL,
                0
        );

        if (rc < -1) {
                vm_panic("error while executing regular expression");
        }

        return BOOLEAN(rc != -1);
}

static struct value
string_match(struct value *string, value_vector *args)
{
        if (args->count != 1) {
                vm_panic("the match method on strings expects 1 argument but got %zu", args->count);
        }

        struct value pattern = args->items[0];

        if (pattern.type != VALUE_REGEX) {
                vm_panic("non-regex passed to the match method on string");
        }

        static int ovec[30];
        char const *s = string->string;
        int len = string->bytes;
        int rc;

        rc = pcre_exec(
                pattern.regex,
                NULL,
                string->string,
                len,
                0,
                0,
                ovec,
                30
        );

        if (rc < -1) {
                vm_panic("error while executing regular expression");
        }

        if (rc == -1) {
                return NIL;
        }

        struct value match;

        if (rc == 1) {
                match = STRINGN(s + ovec[0], ovec[1] - ovec[0]);
        } else {
                match = ARRAY(value_array_new());
                vec_reserve(*match.array, rc);

                int j = 0;
                for (int i = 0; i < rc; ++i, j += 2) {
                        vec_push(*match.array, STRINGN(s + ovec[j], ovec[j + 1] - ovec[j]));
                }
        }

        return match;
}

static struct value
string_matches(struct value *string, value_vector *args)
{
        if (args->count != 1) {
                vm_panic("the matches method on strings expects 1 argument but got %zu", args->count);
        }

        struct value pattern = args->items[0];

        if (pattern.type != VALUE_REGEX) {
                vm_panic("non-regex passed to the matches method on string");
        }

        struct value result = ARRAY(value_array_new());

        static int ovec[30];
        char const *s = string->string;
        int len = string->bytes;
        int rc;

        while ((rc = pcre_exec(
                        pattern.regex,
                        pattern.extra,
                        s,
                        len,
                        0,
                        0,
                        ovec,
                        30
                )) > 0) {

                struct value match;

                if (rc == 1) {
                        match = STRINGN(s + ovec[0], ovec[1] - ovec[0]);
                } else {
                        match = ARRAY(value_array_new());
                        vec_reserve(*match.array, rc);

                        int j = 0;
                        for (int i = 0; i < rc; ++i, j += 2) {
                                vec_push(*match.array, STRINGN(s + ovec[j], ovec[j + 1] - ovec[j]));
                        }
                }

                vec_push(*result.array, match);

                s += ovec[1];
                len -= ovec[1];
        }

        if (rc < -1) {
                vm_panic("error while executing regular expression");
        }

        return result;
}

static struct value
string_char(struct value *string, value_vector *args)
{
        if (args->count != 1) {
                vm_panic("the char method on strings expects 1 argument but got %zu", args->count);
        }

        struct value i = args->items[0];

        if (i.type != VALUE_INTEGER) {
                vm_panic("non-integer passed to the char method on string");
        }

        limitpos.graphemes = i.integer;
        limitpos.bytes = string->bytes;
        tickit_string_count(string->string, &outpos, &limitpos);

        if (outpos.graphemes != i.integer) {
                return NIL;
        }

        int offset = outpos.bytes;

        limitpos.graphemes = 1;
        limitpos.bytes = string->bytes - offset;
        tickit_string_count(string->string + offset, &outpos, &limitpos);

        if (outpos.graphemes != 1) {
                return NIL;
        }

        return STRINGN(string->string + offset, outpos.bytes);
}

static struct value
string_chars(struct value *string, value_vector *args)
{
        if (args->count != 0) {
                vm_panic("the str.chars() method on strings expects 0 arguments but got %zu", args->count);
        }

        struct value result = ARRAY(value_array_new());

        char const *s = string->string;
        int n = string->bytes;

        while (n > 0) {
                limitpos.bytes = n;
                limitpos.graphemes = 1;
                tickit_string_count(s, &outpos, &limitpos);
                vec_push(*result.array, STRINGN(s, outpos.bytes));
                s += outpos.bytes;
                n -= outpos.bytes;
        }

        return result;
}

static struct {
        char const *name;
        struct value (*func)(struct value *, value_vector *);
} funcs[] = {
        { .name = "char",      .func = string_char      },
        { .name = "chars",     .func = string_chars     },
        { .name = "len",       .func = string_length    },
        { .name = "match",     .func = string_match     },
        { .name = "match?",    .func = string_is_match  },
        { .name = "matches",   .func = string_matches   },
        { .name = "replace",   .func = string_replace   },
        { .name = "search",    .func = string_search    },
        { .name = "slice",     .func = string_slice     },
        { .name = "split",     .func = string_split     },
};

static size_t const nfuncs = sizeof funcs / sizeof funcs[0];

struct value (*get_string_method(char const *name))(struct value *, value_vector *)
{
        int lo = 0,
            hi = nfuncs - 1;

        while (lo <= hi) {
                int m = (lo + hi) / 2;
                int c = strcmp(name, funcs[m].name);
                if      (c < 0) hi = m - 1;
                else if (c > 0) lo = m + 1;
                else            return funcs[m].func;
        }

        return NULL;
}
