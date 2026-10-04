#include "cli.h"
#include "file_search.h"
#include "grepmorph.h"

#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *path;
    bool output_failed;
} output_context;

static void print_usage(FILE *stream, const char *program) {
    fprintf(stream,
        "grepmorph %s\n"
        "Representation-aware binary search.\n\n"
        "Usage:\n"
        "  %s [--hex] [--] <query> <file> ...\n"
        "  %s --help\n"
        "  %s --version\n\n"
        "Queries are literal, case-sensitive bytes; escapes are not interpreted.\n"
        "--hex accepts hexadecimal byte pairs, optionally separated by whitespace.\n"
        "-- ends option parsing, allowing a query beginning with '-'.\n"
        "Only explicit regular files are supported in this increment.\n\n"
        "Output: file:0x<16-digit byte offset>:raw\n"
        "Exit codes: 0 = matches, 1 = no matches, 2 = invalid input or I/O error.\n",
        GREPMORPH_VERSION, program, program, program
    );
}

static int hex_digit(unsigned char character) {
    if(character >= '0' && character <= '9') return character - '0';
    if(character >= 'a' && character <= 'f') return character - 'a' + 10;
    if(character >= 'A' && character <= 'F') return character - 'A' + 10;
    return -1;
}

static bool hex_space(unsigned char character) {
    return character == ' ' || character == '\t' || character == '\r' ||
           character == '\n' || character == '\v' || character == '\f';
}

static uint8_t *parse_hex(const char *query, size_t *length) {
    size_t digits = 0;
    for(const unsigned char *p = (const unsigned char *)query; *p != 0; ++p) {
        if(hex_space(*p)) continue;
        if(hex_digit(*p) < 0) return NULL;
        ++digits;
    }
    if(digits == 0 || digits % 2 != 0) return NULL;

    uint8_t *bytes = malloc(digits / 2);
    if(bytes == NULL) return NULL;
    size_t written = 0;
    int high = -1;
    for(const unsigned char *p = (const unsigned char *)query; *p != 0; ++p) {
        if(hex_space(*p)) continue;
        const int digit = hex_digit(*p);
        if(high < 0) {
            high = digit;
        } else {
            bytes[written++] = (uint8_t)(high * 16 + digit);
            high = -1;
        }
    }
    *length = written;
    return bytes;
}

/* Do not let a filename inject terminal control characters into output. */
static void print_path(FILE *stream, const char *path) {
    for(const unsigned char *p = (const unsigned char *)path; *p != 0; ++p) {
        if(*p == '\\') {
            fputs("\\\\", stream);
        } else if(*p < 0x20 || *p == 0x7f) {
            fprintf(stream, "\\x%02x", (unsigned int)*p);
        } else {
            fputc(*p, stream);
        }
    }
}

static void print_match(const gm_match *match, void *context) {
    output_context *output = context;
    if(output->output_failed) return;
    print_path(stdout, output->path);
    if(fprintf(stdout, ":0x%016" PRIx64 ":%s\n", match->offset,
               gm_morph_name(match->morph)) < 0 || ferror(stdout)) {
        output->output_failed = true;
    }
}

static void report_file_error(const char *path, const char *message) {
    fputs("grepmorph: ", stderr);
    print_path(stderr, path);
    fprintf(stderr, ": %s\n", message);
}

static void report_open_error(const char *path, int error) {
#ifdef _MSC_VER
    char message[256];
    if(strerror_s(message, sizeof(message), error) == 0) {
        report_file_error(path, message);
    } else {
        report_file_error(path, "file open failed");
    }
#else
    report_file_error(path, strerror(error));
#endif
}

int gm_cli_run(int argc, char **argv) {
    const char *program = (argc > 0 && argv != NULL && argv[0] != NULL) ?
                          argv[0] : "grepmorph";
    if(argc < 2 || argv == NULL) {
        print_usage(stderr, program);
        return 2;
    }

    bool hex = false;
    int argument = 1;
    for(; argument < argc && argv[argument][0] == '-'; ++argument) {
        if(strcmp(argv[argument], "--") == 0) {
            ++argument;
            break;
        }
        if(strcmp(argv[argument], "--help") == 0) {
            print_usage(stdout, program);
            return fflush(stdout) == 0 ? 0 : 2;
        }
        if(strcmp(argv[argument], "--version") == 0) {
            puts(GREPMORPH_VERSION);
            return fflush(stdout) == 0 ? 0 : 2;
        }
        if(strcmp(argv[argument], "--hex") == 0 && !hex) {
            hex = true;
            continue;
        }
        fputs("grepmorph: unknown or repeated option; use --help\n", stderr);
        return 2;
    }
    if(argc - argument < 2) {
        fputs("grepmorph: provide a nonempty query and at least one file\n", stderr);
        return 2;
    }

    const char *query = argv[argument++];
    size_t needle_length = strlen(query);
    uint8_t *owned_needle = NULL;
    const uint8_t *needle = (const uint8_t *)query;
    if(hex) {
        owned_needle = parse_hex(query, &needle_length);
        if(owned_needle == NULL) {
            fputs("grepmorph: invalid hex query or allocation failure; "
                  "provide a nonempty, even number of hex digits\n", stderr);
            return 2;
        }
        needle = owned_needle;
    } else if(needle_length == 0) {
        fputs("grepmorph: empty query is not allowed\n", stderr);
        return 2;
    }

    bool any_match = false;
    bool any_error = false;
    for(; argument < argc; ++argument) {
        const char *path = argv[argument];
        FILE *stream = gm_open_regular_file(path);
        if(stream == NULL) {
            report_open_error(path, errno);
            any_error = true;
            continue;
        }
        output_context output = {.path = path, .output_failed = false};
        uint64_t count = 0;
        const gm_scan_status status = gm_search_stream(
            stream, needle, needle_length, GM_MORPH_RAW,
            GM_DEFAULT_CHUNK_SIZE, 0, print_match, &output, &count
        );
        if(count != 0) any_match = true;
        if(status != GM_SCAN_OK) {
            report_file_error(path, gm_scan_status_name(status));
            any_error = true;
        }
        if(fclose(stream) != 0) {
            report_file_error(path, "file close failed");
            any_error = true;
        }
        if(output.output_failed) {
            any_error = true;
            break;
        }
    }
    if(fflush(stdout) != 0 || ferror(stdout)) {
        fputs("grepmorph: output write failed\n", stderr);
        any_error = true;
    }
    free(owned_needle);
    return any_error ? 2 : (any_match ? 0 : 1);
}
