#ifndef GREPMORPH_TEST_CHECK_H
#define GREPMORPH_TEST_CHECK_H

#include <stdio.h>
#include <stdlib.h>

/* Unlike assert(), checks remain active when Release builds define NDEBUG. */
#define CHECK(expression) do { \
    if(!(expression)) { \
        fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #expression); \
        exit(EXIT_FAILURE); \
    } \
} while(0)

#endif
