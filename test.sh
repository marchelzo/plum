#!/usr/bin/env bash

test_functions()
{
        grep '^TEST(' src/*.c | grep -v '// OFF' | awk '{ print $1 }' | tr ':' $'\t' | sed -e 's#src/\([^ ]*\)\.c#\1#' -e 's/TEST(\([^ )]*\))/\1/'
}

cat > test.c <<EOF
#include <stdio.h>
#include <stdbool.h>

EOF


mapfile -t funcs < <(test_functions)

for func in "${funcs[@]}"; do
        read -r file testname <<< "$func"
        echo "void TEST_${file}_${testname}(bool *);" >> test.c
done

cat >> test.c <<EOF

int
main(void)
{
        int passed = 0;
        int failed = 0;
        bool b = true;
        printf("\x1b[37;1mRunning \x1b[0m\x1b[32m${#funcs[@]} \x1b[37;1mtests...\x1b[0m\n\n");
EOF

for func in "${funcs[@]}"; do
        read -r file testname <<< "$func"
cat >> test.c <<EOF
        b = true;
        printf("  test ${file}::${testname} ... ");
        fflush(stdout);
        TEST_${file}_${testname}(&b);
        if (b) {
                printf("\x1b[32mok\x1b[0m\n");
                passed += 1;
        } else {
                failed += 1;
        }
EOF
done

cat >> test.c <<EOF

        printf("\n\x1b[32m%3d \x1b[37;1mpassed\x1b[0m\n", passed);
        if (failed == 0) {
                printf("\x1b[32m%3d \x1b[37;1mfailed\x1b[0m\n", failed);
        } else {
                printf("\x1b[31;1m%3d \x1b[37;1mfailed\x1b[0m\n", failed);
        }

        return 0;
}
EOF
