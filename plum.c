#include <locale.h>

#include "editor.h"
#include "render.h"
#include "term.h"
#include "log.h"

int main(void)
{
        setlocale(LC_ALL, "");

        struct editor e;

        term_init(&e);
        editor_init(&e, term_height(), term_width());

        render(&e);

        editor_run(&e);

        return 0;
}
