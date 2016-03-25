#include <stdio.h>
#include <string.h>

#include "vm.h"

int
main(void)
{
        char buffer[8192];
        char stmtbuf[8192];

        vm_init();
        while (fputs("> ", stdout), fflush(stdout), (fgets(buffer, 8192, stdin) != NULL) && sprintf(stmtbuf, "print(%s);", buffer)) {
                if (vm_execute(stmtbuf)) {
                        fputs(vm_get_output(), stdout);
                } else if ((strstr(vm_error(), "ParseError") != NULL) && vm_execute(buffer)) {
                        fputs(vm_get_output(), stdout);
                        printf("ok\n");
                } else {
                        printf("%s\n", vm_error());
                }
        }

        return 0;
}
