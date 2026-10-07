#include "file_search.h"
#include "morph.h"
#include "test_check.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
    gm_match values[1024];
    size_t count;
} matches;
static const char *fixture_path;

static void collect(const gm_match *match, void *context) {
    matches *result = context;
    CHECK(result->count < 1024);
    result->values[result->count++] = *match;
}

static FILE *fixture(const uint8_t *data, size_t length) {
    FILE *stream = NULL;
#ifdef _MSC_VER
    CHECK(fopen_s(&stream, fixture_path, "w+b") == 0);
#else
    stream = fopen(fixture_path, "w+b");
#endif
    CHECK(stream != NULL);
    if(length != 0) CHECK(fwrite(data, 1, length, stream) == length);
    CHECK(fseek(stream, 0, SEEK_SET) == 0);
    return stream;
}

static int compare_match(const void *a, const void *b) {
    const gm_match *left = a, *right = b;
    if(left->offset != right->offset) return left->offset < right->offset ? -1 : 1;
    if(left->morph != right->morph) return left->morph < right->morph ? -1 : 1;
    return 0;
}

static void compare(const uint8_t *data, size_t length, const gm_pattern *patterns,
                    size_t pattern_count, size_t chunk, uint64_t base) {
    matches expected = {0}, actual = {0};
    /* Independent single-pattern searches, then sorting, not the stream loop. */
    for(size_t i = 0; i < pattern_count; ++i) {
        (void)gm_search_exact(data, length, patterns[i].bytes, patterns[i].length,
                              patterns[i].morph, collect, &expected);
    }
    qsort(expected.values, expected.count, sizeof(expected.values[0]), compare_match);
    FILE *stream = fixture(data, length);
    uint64_t count = 99;
    CHECK(gm_search_stream_patterns(stream, patterns, pattern_count, chunk, base,
                                    collect, &actual, &count) == GM_SCAN_OK);
    CHECK(count == expected.count && actual.count == expected.count);
    for(size_t i = 0; i < expected.count; ++i) {
        CHECK(actual.values[i].offset == base + expected.values[i].offset);
        CHECK(actual.values[i].length == expected.values[i].length);
        CHECK(actual.values[i].morph == expected.values[i].morph);
    }
    CHECK(fclose(stream) == 0);
}

static void text_boundaries(void) {
    const uint8_t query[] = {'a', 0xc3, 0xa9, 0xf0, 0x9f, 0x98, 0x80};
    gm_morph_set set = {0};
    CHECK(gm_morph_generate(query, sizeof(query), GM_SELECT_TEXT, &set, NULL) == GM_MORPH_OK);
    uint8_t data[128];
    for(size_t offset = 0; offset < 20; ++offset) {
        memset(data, 0xff, sizeof(data));
        size_t at = offset;
        for(size_t i = 0; i < set.count; ++i) {
            memcpy(data + at, set.patterns[i].bytes, set.patterns[i].length);
            at += set.patterns[i].length + 3;
        }
        for(size_t chunk = 1; chunk < 17; ++chunk) {
            compare(data, at, set.patterns, set.count, chunk, UINT64_C(0x100000000));
        }
    }
    gm_morph_set_free(&set);
}

static void tails_and_overlaps(void) {
    const uint8_t data[] = "aaaaaaaaaa";
    const gm_pattern patterns[] = {
        {(const uint8_t *)"a", 1, GM_MORPH_RAW},
        {(const uint8_t *)"aa", 2, GM_MORPH_UTF8},
        {(const uint8_t *)"aaaaa", 5, GM_MORPH_UTF16_LE},
        {(const uint8_t *)"aaaaaaaaaaaaaaaa", 16, GM_MORPH_UTF16_BE}
    };
    for(size_t length = 0; length < sizeof(data); ++length) {
        for(size_t chunk = 1; chunk <= 17; ++chunk) compare(data, length, patterns, 4, chunk, 0);
    }
}

static void order_and_identical(void) {
    const gm_pattern patterns[] = {
        {(const uint8_t *)"a", 1, GM_MORPH_UTF16_BE},
        {(const uint8_t *)"a", 1, GM_MORPH_RAW}
    };
    FILE *stream = fixture((const uint8_t *)"aa", 2);
    matches result = {0};
    uint64_t count;
    CHECK(gm_search_stream_patterns(stream, patterns, 2, 1, 0,
                                    collect, &result, &count) == GM_SCAN_OK);
    CHECK(count == 4 && result.count == 4);
    for(size_t i = 0; i < 4; ++i) {
        CHECK(result.values[i].offset == i / 2);
        CHECK(result.values[i].morph == patterns[i % 2].morph);
    }
    CHECK(fclose(stream) == 0);
}

static void high_offsets(void) {
    const gm_pattern patterns[] = {
        {(const uint8_t *)"a", 1, GM_MORPH_RAW},
        {(const uint8_t *)"aaa", 3, GM_MORPH_UTF16_LE}
    };
    for(size_t chunk = 1; chunk < 6; ++chunk) {
        compare((const uint8_t *)"aaa", 3, patterns, 2, chunk, UINT64_MAX - 2);
    }
    FILE *stream = fixture((const uint8_t *)"aaa", 3);
    uint64_t count;
    CHECK(gm_search_stream_patterns(stream, patterns, 2, 3, UINT64_MAX - 1,
                                    NULL, NULL, &count) == GM_SCAN_OFFSET_OVERFLOW);
    CHECK(count == 0);
    CHECK(fclose(stream) == 0);
}

static void invalid(void) {
    const uint8_t byte = 0;
    gm_pattern pattern = {&byte, 1, GM_MORPH_RAW};
    FILE *stream = fixture(NULL, 0);
    uint64_t count = 123;
    CHECK(gm_search_stream_patterns(NULL, &pattern, 1, 1, 0, NULL, NULL, &count) == GM_SCAN_INVALID_ARGUMENT);
    CHECK(count == 0);
    CHECK(gm_search_stream_patterns(stream, NULL, 1, 1, 0, NULL, NULL, &count) == GM_SCAN_INVALID_ARGUMENT);
    CHECK(gm_search_stream_patterns(stream, &pattern, 0, 1, 0, NULL, NULL, &count) == GM_SCAN_INVALID_ARGUMENT);
    CHECK(gm_search_stream_patterns(stream, &pattern, 1, 0, 0, NULL, NULL, &count) == GM_SCAN_INVALID_ARGUMENT);
    CHECK(gm_search_stream_patterns(stream, &pattern, 1, 1, 0, NULL, NULL, NULL) == GM_SCAN_INVALID_ARGUMENT);
    pattern.length = 0;
    CHECK(gm_search_stream_patterns(stream, &pattern, 1, 1, 0, NULL, NULL, &count) == GM_SCAN_INVALID_ARGUMENT);
    pattern.length = SIZE_MAX;
    CHECK(gm_search_stream_patterns(stream, &pattern, 1, 2, 0, NULL, NULL, &count) == GM_SCAN_INVALID_ARGUMENT);
    pattern.length = 1; pattern.bytes = NULL;
    CHECK(gm_search_stream_patterns(stream, &pattern, 1, 1, 0, NULL, NULL, &count) == GM_SCAN_INVALID_ARGUMENT);
    CHECK(fclose(stream) == 0);
}

static uint32_t state = 0x9876abcdu;
static uint32_t next_random(void) {
    state ^= state << 13; state ^= state >> 17; state ^= state << 5;
    return state;
}

static void differential(void) {
    uint8_t data[96], bytes[4][17];
    gm_pattern patterns[4];
    for(size_t trial = 0; trial < 200; ++trial) {
        const size_t length = next_random() % 97u;
        for(size_t i = 0; i < length; ++i) data[i] = (uint8_t)(next_random() % 4u);
        for(size_t p = 0; p < 4; ++p) {
            const size_t n = 1u + next_random() % 17u;
            for(size_t i = 0; i < n; ++i) bytes[p][i] = (uint8_t)(next_random() % 4u);
            patterns[p] = (gm_pattern){bytes[p], n, (gm_morph)p};
            if(n <= length && trial % 2 == 0) memcpy(data + length - n, bytes[p], n);
        }
        for(size_t chunk = 1; chunk <= 22; chunk += 3) {
            compare(data, length, patterns, 4, chunk, UINT64_C(0x100000000));
        }
    }
}

static void no_sink(void) {
    const gm_pattern patterns[] = {
        {(const uint8_t *)"a", 1, GM_MORPH_RAW},
        {(const uint8_t *)"aa", 2, GM_MORPH_UTF8}
    };
    FILE *stream = fixture((const uint8_t *)"aaa", 3);
    uint64_t count;
    CHECK(gm_search_stream_patterns(stream, patterns, 2, 1, 0, NULL, NULL, &count) == GM_SCAN_OK);
    CHECK(count == 5);
    CHECK(fclose(stream) == 0);
}

int main(int argc, char **argv) {
    CHECK(argc == 3);
    fixture_path = argv[2];
    if(strcmp(argv[1], "text_boundaries") == 0) text_boundaries();
    else if(strcmp(argv[1], "tails_and_overlaps") == 0) tails_and_overlaps();
    else if(strcmp(argv[1], "order_and_identical") == 0) order_and_identical();
    else if(strcmp(argv[1], "high_offsets") == 0) high_offsets();
    else if(strcmp(argv[1], "invalid") == 0) invalid();
    else if(strcmp(argv[1], "differential") == 0) differential();
    else if(strcmp(argv[1], "no_sink") == 0) no_sink();
    else CHECK(false);
    CHECK(remove(fixture_path) == 0);
    return 0;
}
