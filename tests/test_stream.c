#include "file_search.h"
#include "test_check.h"

#include <string.h>
#ifndef _WIN32
#include <unistd.h>
#endif

typedef struct {
    gm_match values[256];
    size_t count;
} collected_matches;

static const char *fixture_path;

static void collect(const gm_match *match, void *context) {
    collected_matches *matches = context;
    CHECK(matches->count < sizeof(matches->values) / sizeof(matches->values[0]));
    matches->values[matches->count++] = *match;
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

static void compare(const uint8_t *data, size_t length, const uint8_t *needle,
                    size_t needle_length, size_t chunk_size, uint64_t base) {
    collected_matches expected = {0};
    collected_matches actual = {0};
    const size_t expected_count = gm_search_exact(data, length, needle, needle_length,
                                                  GM_MORPH_RAW, collect, &expected);
    FILE *stream = fixture(data, length);
    uint64_t count = 999;
    CHECK(gm_search_stream(stream, needle, needle_length, GM_MORPH_RAW, chunk_size,
                           base, collect, &actual, &count) == GM_SCAN_OK);
    CHECK(count == (uint64_t)expected_count && actual.count == expected.count);
    for(size_t i = 0; i < actual.count; ++i) {
        CHECK(actual.values[i].offset == expected.values[i].offset + base);
        CHECK(actual.values[i].length == needle_length);
        CHECK(actual.values[i].morph == GM_MORPH_RAW);
    }
    CHECK(fclose(stream) == 0);
}

static void boundary(void) {
    const uint8_t data[] = "xxabc--abcxabc";
    for(size_t chunk = 1; chunk <= sizeof(data) + 1; ++chunk) {
        compare(data, sizeof(data) - 1, (const uint8_t *)"abc", 3, chunk, 0);
    }
}

static void overlap(void) {
    for(size_t chunk = 1; chunk <= 6; ++chunk) {
        compare((const uint8_t *)"aaaaaaaa", 8, (const uint8_t *)"aaa", 3, chunk, 0);
    }
}

static void long_pattern(void) {
    compare((const uint8_t *)"xabcdefghijkabcdefghijk", 23,
            (const uint8_t *)"abcdefghijk", 11, 2, 0);
}

static void empty_and_short(void) {
    compare(NULL, 0, (const uint8_t *)"abc", 3, 1, 0);
    compare((const uint8_t *)"ab", 2, (const uint8_t *)"abc", 3, 1, 0);
    compare((const uint8_t *)"abc", 3, (const uint8_t *)"abc", 3, 1, 0);
    compare((const uint8_t *)"a", 1, (const uint8_t *)"a", 1, 1, 0);
}

static void binary(void) {
    const uint8_t data[] = {0, 0xff, 0, 0xff, 0, 0x80};
    const uint8_t needle[] = {0, 0xff, 0};
    for(size_t chunk = 1; chunk < 8; ++chunk) {
        compare(data, sizeof(data), needle, sizeof(needle), chunk, 0);
    }
}

static void invalid(void) {
    const uint8_t needle = 0;
    FILE *stream = fixture(NULL, 0);
    uint64_t count = 123;
    CHECK(gm_search_stream(NULL, &needle, 1, GM_MORPH_RAW, 4, 0, NULL, NULL,
                           &count) == GM_SCAN_INVALID_ARGUMENT);
    CHECK(count == 0);
    CHECK(gm_search_stream(stream, NULL, 1, GM_MORPH_RAW, 4, 0, NULL, NULL,
                           &count) == GM_SCAN_INVALID_ARGUMENT);
    CHECK(gm_search_stream(stream, &needle, 0, GM_MORPH_RAW, 4, 0, NULL, NULL,
                           &count) == GM_SCAN_INVALID_ARGUMENT);
    CHECK(gm_search_stream(stream, &needle, 1, GM_MORPH_RAW, 0, 0, NULL, NULL,
                           &count) == GM_SCAN_INVALID_ARGUMENT);
    CHECK(gm_search_stream(stream, &needle, 1, GM_MORPH_RAW, 4, 0, NULL, NULL,
                           NULL) == GM_SCAN_INVALID_ARGUMENT);
    CHECK(gm_search_stream(stream, &needle, SIZE_MAX, GM_MORPH_RAW, 2, 0, NULL, NULL,
                           &count) == GM_SCAN_INVALID_ARGUMENT);
    CHECK(fclose(stream) == 0);
}

static void offsets_64(void) {
    compare((const uint8_t *)"abc-abc", 7, (const uint8_t *)"abc", 3, 2,
            UINT64_C(0x100000000));
    compare((const uint8_t *)"abc", 3, (const uint8_t *)"abc", 3, 1, UINT64_MAX - 2);
    compare((const uint8_t *)"a", 1, (const uint8_t *)"a", 1, 1, UINT64_MAX);
}

static void offset_overflow(void) {
    FILE *stream = fixture((const uint8_t *)"abc", 3);
    uint64_t count = 42;
    CHECK(gm_search_stream(stream, (const uint8_t *)"a", 1, GM_MORPH_RAW, 3,
                           UINT64_MAX - 1, NULL, NULL, &count) == GM_SCAN_OFFSET_OVERFLOW);
    CHECK(count == 0);
    CHECK(fclose(stream) == 0);
}

static void no_sink(void) {
    FILE *stream = fixture((const uint8_t *)"aaa", 3);
    uint64_t count = 0;
    CHECK(gm_search_stream(stream, (const uint8_t *)"aa", 2, GM_MORPH_RAW, 1,
                           0, NULL, NULL, &count) == GM_SCAN_OK);
    CHECK(count == 2);
    CHECK(fclose(stream) == 0);
}

static uint32_t random_state = UINT32_C(0xa541038f);
static uint32_t next_random(void) {
    random_state ^= random_state << 13;
    random_state ^= random_state >> 17;
    random_state ^= random_state << 5;
    return random_state;
}

static void differential(void) {
    uint8_t data[96];
    uint8_t needle[17];
    for(size_t trial = 0; trial < 160; ++trial) {
        const size_t length = (size_t)(next_random() % 97);
        const size_t needle_length = 1 + (size_t)(next_random() % 17);
        for(size_t i = 0; i < length; ++i) data[i] = (uint8_t)(next_random() % 4);
        for(size_t i = 0; i < needle_length; ++i) needle[i] = (uint8_t)(next_random() % 4);
        if(trial % 2 == 0 && needle_length <= length) {
            memcpy(data + length - needle_length, needle, needle_length);
        }
        for(size_t chunk = 1; chunk <= 19; chunk += 3) {
            compare(data, length, needle, needle_length, chunk, UINT64_C(0x100000000));
        }
    }
}

#ifndef _WIN32
static void read_error(void) {
    FILE *stream = fixture(NULL, 0);
    CHECK(close(fileno(stream)) == 0);
    uint64_t count = 0;
    CHECK(gm_search_stream(stream, (const uint8_t *)"a", 1, GM_MORPH_RAW, 4,
                           0, NULL, NULL, &count) == GM_SCAN_IO_ERROR);
    CHECK(count == 0);
    /* No descriptor is opened between close and fclose, so it cannot be reused. */
    (void)fclose(stream);
}
#endif

int main(int argc, char **argv) {
    CHECK(argc == 3);
    fixture_path = argv[2];
    if(strcmp(argv[1], "boundary") == 0) boundary();
    else if(strcmp(argv[1], "overlap") == 0) overlap();
    else if(strcmp(argv[1], "long_pattern") == 0) long_pattern();
    else if(strcmp(argv[1], "empty_and_short") == 0) empty_and_short();
    else if(strcmp(argv[1], "binary") == 0) binary();
    else if(strcmp(argv[1], "invalid") == 0) invalid();
    else if(strcmp(argv[1], "offsets_64") == 0) offsets_64();
    else if(strcmp(argv[1], "offset_overflow") == 0) offset_overflow();
    else if(strcmp(argv[1], "no_sink") == 0) no_sink();
    else if(strcmp(argv[1], "differential") == 0) differential();
#ifndef _WIN32
    else if(strcmp(argv[1], "read_error") == 0) read_error();
#endif
    else CHECK(0);
    CHECK(remove(fixture_path) == 0);
    return 0;
}
