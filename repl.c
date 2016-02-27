#include <stdio.h>
#include <string.h>

#include "interpreter.h"
#include "value.h"

int
main(void)
{
        char buffer[8192];

        interpreter_init();
        struct value v;
        while (fputs("> ", stdout), fflush(stdout), (fgets(buffer, 8192, stdin) != NULL)) {
                if (interpreter_eval_source(buffer, &v)) {
                        printf("%s\n", value_show(&v));
                } else if ((strstr(interpreter_error(), "Parse error") != NULL) && interpreter_execute_source(buffer)) {
                        printf("ok\n");
                } else {
                        printf("%s\n", interpreter_error());
                }
        }

        return 0;
}
