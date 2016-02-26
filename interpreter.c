#include <stdio.h>

#include "interpreter.h"

int
main(int argc, char **argv)
{
        if (argc < 2) {
                printf("error: specify a file to run\n");
                return -1;
        }

        interpreter_init();
        if (!interpreter_execute_file(argv[1])) {
                fprintf(stderr, "%s\n", interpreter_error());
                return -1;
        }

        return 0;
}
