#include "cli.h"
#include "file_search.h"
#include "grepmorph.h"
#include "morph.h"
#include "walk.h"

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
        "  %s [options] [--] <query> <path> ...\n"
        "  %s --help\n"
        "  %s --version\n\n"
        "Queries are literal, case-sensitive bytes; escapes are not interpreted.\n"
        "--hex accepts hexadecimal byte pairs, optionally separated by whitespace.\n"
        "--text searches validated UTF-8 text as utf8, utf16-le and utf16-be.\n"
        "--morph NAME selects a representation; repeat for a subset:\n"
        "  raw, utf8, utf16-le, utf16-be, hex-text, uint8,\n"
        "  uint16-le, uint16-be, uint32-le, uint32-be, uint64-le, uint64-be.\n"
        "hex-text encodes query bytes as contiguous hex; only A-F ignore case.\n"
        "Integer morphs accept unsigned decimal or 0x hex; every width must fit.\n"
        "--hex may combine with --morph raw and/or --morph hex-text only.\n"
        "--text cannot combine with --hex or --morph.\n"
        "Text matching is exact: no case folding, normalisation or BOM required.\n"
        "-- ends option parsing, allowing a query beginning with '-'.\n"
        "-r, --recursive searches directories as well as files.\n"
        "Explicit paths follow links; discovered links and special files are skipped.\n"
        "Hidden files are included; no ignore rules are applied.\n\n"
        "Output: file:0x<16-digit byte offset>:<morph>\n"
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

typedef struct {
    const gm_morph_set *morphs;
    bool any_match;
    bool any_error;
    bool output_failed;
} search_context;

static void report_path_error(const char *path, const char *message, void *context) {
    search_context *search = context;
    report_file_error(path, message);
    search->any_error = true;
}

static bool search_file(const char *path, bool explicit_input, void *context) {
    search_context *search = context;
    FILE *stream = gm_open_regular_file_with_links(path, explicit_input);
    if(stream == NULL) {
        report_open_error(path, errno);
        search->any_error = true;
        return true;
    }
    output_context output = {.path = path, .output_failed = false};
    uint64_t count = 0;
    const gm_scan_status status = gm_search_stream_patterns(
        stream, search->morphs->patterns, search->morphs->count,
        GM_DEFAULT_CHUNK_SIZE, 0, print_match, &output, &count
    );
    if(count != 0) search->any_match = true;
    if(status != GM_SCAN_OK) report_path_error(path, gm_scan_status_name(status), search);
    if(fclose(stream) != 0) report_path_error(path, "file close failed", search);
    if(output.output_failed) {
        search->any_error = true;
        search->output_failed = true;
        return false;
    }
    return true;
}

int gm_cli_run(int argc, char **argv) {
    const char *program = (argc > 0 && argv != NULL && argv[0] != NULL) ?
                          argv[0] : "grepmorph";
    if(argc < 2 || argv == NULL) {
        print_usage(stderr, program);
        return 2;
    }

    bool hex = false;
    bool text = false;
    unsigned int selection = 0;
    bool recursive = false;
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
        if(strcmp(argv[argument], "--hex") == 0 && !hex && !text) {
            hex = true;
            continue;
        }
        if(strcmp(argv[argument], "--text") == 0 && !hex && !text && selection == 0) {
            text = true;
            continue;
        }
        if(strcmp(argv[argument], "--morph") == 0 && !text) {
            if(argument + 1 >= argc) {
                fputs("grepmorph: --morph requires a name\n", stderr);
                return 2;
            }
            const char *name = argv[++argument];
            unsigned int bit = 0;
            if(strcmp(name, "raw") == 0) bit = GM_SELECT_RAW;
            else if(strcmp(name, "utf8") == 0) bit = GM_SELECT_UTF8;
            else if(strcmp(name, "utf16-le") == 0) bit = GM_SELECT_UTF16_LE;
            else if(strcmp(name, "utf16-be") == 0) bit = GM_SELECT_UTF16_BE;
            else if(strcmp(name, "hex-text") == 0) bit = GM_SELECT_HEX_TEXT;
            else if(strcmp(name, "uint8") == 0) bit = GM_SELECT_UINT8;
            else if(strcmp(name, "uint16-le") == 0) bit = GM_SELECT_UINT16_LE;
            else if(strcmp(name, "uint16-be") == 0) bit = GM_SELECT_UINT16_BE;
            else if(strcmp(name, "uint32-le") == 0) bit = GM_SELECT_UINT32_LE;
            else if(strcmp(name, "uint32-be") == 0) bit = GM_SELECT_UINT32_BE;
            else if(strcmp(name, "uint64-le") == 0) bit = GM_SELECT_UINT64_LE;
            else if(strcmp(name, "uint64-be") == 0) bit = GM_SELECT_UINT64_BE;
            if(bit == 0 || (selection & bit) != 0) {
                fputs("grepmorph: unsupported or repeated morph; use --help\n", stderr);
                return 2;
            }
            selection |= bit;
            continue;
        }
        if((strcmp(argv[argument], "-r") == 0 ||
            strcmp(argv[argument], "--recursive") == 0) && !recursive) {
            recursive = true;
            continue;
        }
        fputs("grepmorph: unknown, repeated or conflicting option; use --help\n", stderr);
        return 2;
    }
    if(hex && (selection & ~(GM_SELECT_RAW | GM_SELECT_HEX_TEXT)) != 0) {
        fputs("grepmorph: --hex supports only raw and hex-text morphs\n", stderr);
        return 2;
    }
    if(argc - argument < 2) {
        fputs("grepmorph: provide a nonempty query and at least one path\n", stderr);
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

    if(text) selection = GM_SELECT_TEXT;
    if(selection == 0) selection = GM_SELECT_RAW;
    gm_morph_set morphs = {0};
    size_t error_offset = 0;
    const gm_morph_status compiled = gm_morph_generate(
        needle, needle_length, selection, &morphs, &error_offset
    );
    free(owned_needle);
    if(compiled != GM_MORPH_OK) {
        fprintf(stderr, "grepmorph: %s", gm_morph_status_name(compiled));
        if(compiled == GM_MORPH_INVALID_UTF8 || compiled == GM_MORPH_INVALID_INTEGER) {
            fprintf(stderr, " at byte %zu", error_offset);
        }
        fputc('\n', stderr);
        return 2;
    }
    search_context search = {.morphs = &morphs};
    for(; argument < argc && !search.output_failed; ++argument) {
        const gm_walk_status status = gm_walk_path(
            argv[argument], recursive, search_file, report_path_error, &search
        );
        if(status != GM_WALK_OK) search.any_error = true;
    }
    if(fflush(stdout) != 0 || ferror(stdout)) {
        fputs("grepmorph: output write failed\n", stderr);
        search.any_error = true;
    }
    gm_morph_set_free(&morphs);
    return search.any_error ? 2 : (search.any_match ? 0 : 1);
}
