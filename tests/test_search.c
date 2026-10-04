#include "grepmorph.h"
#include "test_check.h"

#include <string.h>

typedef struct {
    gm_match values[64];
    size_t count;
} collected_matches;

static void collect(const gm_match *match, void *context) {
    collected_matches *matches = context;
    CHECK(matches->count < sizeof(matches->values) / sizeof(matches->values[0]));
    matches->values[matches->count++] = *match;
}

static void basic(void) {
    const uint8_t data[] = "abc--abc-ABC";
    collected_matches matches = {0};
    CHECK(gm_search_exact(data, sizeof(data) - 1, (const uint8_t *)"abc", 3,
                          GM_MORPH_RAW, collect, &matches) == 2);
    CHECK(matches.count == 2);
    CHECK(matches.values[0].offset == 0);
    CHECK(matches.values[1].offset == 5);
    CHECK(matches.values[0].length == 3);
    CHECK(matches.values[0].morph == GM_MORPH_RAW);
}

static void binary(void) {
    const uint8_t data[] = {0x00, 0xff, 0x00, 0xff, 0x00};
    const uint8_t needle[] = {0x00, 0xff, 0x00};
    collected_matches matches = {0};
    CHECK(gm_search_exact(data, sizeof(data), needle, sizeof(needle), GM_MORPH_RAW,
                          collect, &matches) == 2);
    CHECK(matches.values[0].offset == 0 && matches.values[1].offset == 2);
}

static void overlap(void) {
    collected_matches matches = {0};
    CHECK(gm_search_exact((const uint8_t *)"aaaaa", 5, (const uint8_t *)"aaa", 3,
                          GM_MORPH_RAW, collect, &matches) == 3);
    CHECK(matches.count == 3);
    for(size_t i = 0; i < matches.count; ++i) CHECK(matches.values[i].offset == i);
}

static void boundaries(void) {
    const uint8_t data[] = {'x', 'a', 'b', 'c'};
    const uint8_t needle[] = {'a', 'b', 'c'};
    collected_matches matches = {0};
    CHECK(gm_search_exact(data, 4, needle, 3, GM_MORPH_RAW, collect, &matches) == 1);
    CHECK(matches.values[0].offset == 1);
    CHECK(gm_search_exact(needle, 3, needle, 3, GM_MORPH_RAW, NULL, NULL) == 1);
    CHECK(gm_search_exact((const uint8_t *)"xxaZZ", 5, needle, 3,
                          GM_MORPH_RAW, NULL, NULL) == 0);
}

static void invalid(void) {
    const uint8_t byte = 'a';
    CHECK(gm_search_exact(NULL, 1, &byte, 1, GM_MORPH_RAW, NULL, NULL) == 0);
    CHECK(gm_search_exact(&byte, 1, NULL, 1, GM_MORPH_RAW, NULL, NULL) == 0);
    CHECK(gm_search_exact(&byte, 1, &byte, 0, GM_MORPH_RAW, NULL, NULL) == 0);
    CHECK(gm_search_exact(&byte, 0, &byte, 1, GM_MORPH_RAW, NULL, NULL) == 0);
    CHECK(gm_search_exact(&byte, 1, &byte, SIZE_MAX, GM_MORPH_RAW, NULL, NULL) == 0);
}

static void tiny_allocations(void) {
    for(size_t length = 1; length <= 3; ++length) {
        uint8_t *data = malloc(length);
        uint8_t *needle = malloc(length);
        CHECK(data != NULL && needle != NULL);
        memset(data, 'a', length);
        memset(needle, 'a', length);
        CHECK(gm_search_exact(data, length, needle, length,
                              GM_MORPH_RAW, NULL, NULL) == 1);
        CHECK(gm_search_exact(data, length, needle, 1,
                              GM_MORPH_RAW, NULL, NULL) == length);
        needle[length - 1] = 'b';
        CHECK(gm_search_exact(data, length, needle, length,
                              GM_MORPH_RAW, NULL, NULL) == 0);
        free(needle);
        free(data);
    }
}

static void morph_labels(void) {
    const char *names[] = {"raw", "utf8", "utf16-le", "utf16-be", "hex-text",
                           "base64-decoded", "uint-le", "uint-be"};
    for(size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
        CHECK(strcmp(gm_morph_name((gm_morph)i), names[i]) == 0);
        const uint8_t byte = 0;
        collected_matches matches = {0};
        CHECK(gm_search_exact(&byte, 1, &byte, 1, (gm_morph)i, collect, &matches) == 1);
        CHECK(matches.values[0].morph == (gm_morph)i);
    }
    CHECK(strcmp(gm_morph_name((gm_morph)999), "unknown") == 0);
}

int main(int argc, char **argv) {
    CHECK(argc == 2);
    if(strcmp(argv[1], "basic") == 0) basic();
    else if(strcmp(argv[1], "binary") == 0) binary();
    else if(strcmp(argv[1], "overlap") == 0) overlap();
    else if(strcmp(argv[1], "boundaries") == 0) boundaries();
    else if(strcmp(argv[1], "invalid") == 0) invalid();
    else if(strcmp(argv[1], "tiny_allocations") == 0) tiny_allocations();
    else if(strcmp(argv[1], "morph_labels") == 0) morph_labels();
    else CHECK(0);
    return 0;
}
