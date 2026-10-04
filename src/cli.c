#include "cli.h"
#include "grepmorph.h"

#include <stdio.h>
#include <string.h>

static void print_usage(FILE *stream, const char *program) {
    fprintf(stream,
        "grepmorph %s\n"
        "Representation-aware binary search.\n\n"
        "Usage:\n"
        "  %s --help\n"
        "  %s --version\n\n"
        "The search pipeline is not wired in this scaffold yet.\n",
        GREPMORPH_VERSION,
        program,
        program
    );
}

int gm_cli_run(int argc, char **argv) {
    const char *program = (argc > 0 && argv[0] != NULL) ? argv[0] : "grepmorph";

    if(argc == 2 && strcmp(argv[1], "--version") == 0) {
        puts(GREPMORPH_VERSION);
        return 0;
    }

    if(argc == 1 || (argc == 2 && strcmp(argv[1], "--help") == 0)) {
        print_usage(stdout, program);
        return 0;
    }

    fprintf(stderr, "grepmorph: search CLI not implemented yet\n");
    print_usage(stderr, program);
    return 2;
}
