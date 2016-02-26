#ifndef LEX_H_INCLUDED
#define LEX_H_INCLUDED

char const *
lex_error(void);

struct token *
lex(char const *s);

#endif
