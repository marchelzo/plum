#include <stdio.h>

#include "vm.h"
#include "util.h"

int
main(int argc, char **argv)
{
        if (argc < 2) {
                printf("error: specify a file to run\n");
                return -1;
        }

        vm_init();
        if (!vm_execute(slurp(argv[1]))) {
                fprintf(stderr, "%s\n", vm_error());
                return -1;
        }

        return 0;
}
