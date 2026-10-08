#include "morph.h"
#include "file_search.h"
#include "test_check.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

static const char *fixture_path;
static const gm_morph numeric_kinds[] = {
    GM_MORPH_UINT8, GM_MORPH_UINT16_LE, GM_MORPH_UINT16_BE,
    GM_MORPH_UINT32_LE, GM_MORPH_UINT32_BE, GM_MORPH_UINT64_LE, GM_MORPH_UINT64_BE
};
static const size_t widths[] = {1, 2, 2, 4, 4, 8, 8};

static gm_morph_set generate(const char *query, unsigned int selection) {
    gm_morph_set set = {0};
    CHECK(gm_morph_generate((const uint8_t *)query, strlen(query), selection, &set, NULL) == GM_MORPH_OK);
    return set;
}

static void hex_bytes(void) {
    const uint8_t query[] = {'A', 'z', 0, 0xff};
    gm_morph_set set = {0};
    CHECK(gm_morph_generate(query, sizeof(query), GM_SELECT_RAW | GM_SELECT_HEX_TEXT, &set, NULL) == GM_MORPH_OK);
    CHECK(set.count == 2 && set.patterns[1].length == 8);
    CHECK(set.patterns[0].comparison == GM_COMPARE_EXACT);
    CHECK(set.patterns[1].comparison == GM_COMPARE_HEX_TEXT);
    CHECK(set.patterns[1].morph == GM_MORPH_HEX_TEXT);
    CHECK(memcmp(set.patterns[1].bytes, "417a00ff", 8) == 0);
    CHECK(gm_pattern_matches((const uint8_t *)"417A00fF", 8, &set.patterns[1]));
    gm_morph_set_free(&set);
}

static void hex_all_bytes(void) {
    uint8_t query[256];
    for(size_t i = 0; i < sizeof(query); ++i) query[i] = (uint8_t)i;
    gm_morph_set set = {0};
    CHECK(gm_morph_generate(query, sizeof(query), GM_SELECT_HEX_TEXT, &set, NULL) == GM_MORPH_OK);
    CHECK(set.count == 1 && set.patterns[0].length == 512);
    for(size_t i = 0; i < 256; ++i) {
        char expected[3];
        CHECK(snprintf(expected, sizeof(expected), "%02x", (unsigned int)i) == 2);
        CHECK(memcmp(set.patterns[0].bytes + i * 2, expected, 2) == 0);
    }
    gm_morph_set_free(&set);
}

static void hex_overflow(void) {
    const uint8_t byte = 0;
    gm_morph_set set = {0};
    CHECK(gm_morph_generate(&byte, SIZE_MAX / 2 + 1, GM_SELECT_HEX_TEXT, &set, NULL) == GM_MORPH_SIZE_OVERFLOW);
    CHECK(gm_morph_generate(&byte, SIZE_MAX / 3 + 1, GM_SELECT_RAW | GM_SELECT_HEX_TEXT, &set, NULL) == GM_MORPH_SIZE_OVERFLOW);
    CHECK(set.storage == NULL && set.count == 0);
}

static void compare_rules(void) {
    gm_pattern pattern = {(const uint8_t *)"aBcDeF09", 8, GM_MORPH_HEX_TEXT, GM_COMPARE_HEX_TEXT};
    CHECK(gm_pattern_matches((const uint8_t *)"AbCdEf09", 8, &pattern));
    pattern.comparison = GM_COMPARE_EXACT;
    CHECK(!gm_pattern_matches((const uint8_t *)"AbCdEf09", 8, &pattern));
    CHECK(gm_pattern_matches((const uint8_t *)"aBcDeF09", 8, &pattern));
    pattern = (gm_pattern){(const uint8_t *)"g", 1, GM_MORPH_HEX_TEXT, GM_COMPARE_HEX_TEXT};
    CHECK(!gm_pattern_matches((const uint8_t *)"G", 1, &pattern));
    pattern.bytes = (const uint8_t *)"[";
    CHECK(!gm_pattern_matches((const uint8_t *)"{", 1, &pattern));
    pattern.comparison = (gm_comparison)99;
    CHECK(!gm_pattern_matches((const uint8_t *)"[", 1, &pattern));
    CHECK(!gm_pattern_matches(NULL, 1, &pattern));
    CHECK(!gm_pattern_matches((const uint8_t *)"[", 1, NULL));
    pattern.comparison = GM_COMPARE_EXACT;
    pattern.length = 0;
    CHECK(!gm_pattern_matches((const uint8_t *)"[", 1, &pattern));
    pattern.length = 2;
    CHECK(!gm_pattern_matches((const uint8_t *)"[", 1, &pattern));
    pattern.bytes = NULL;
    CHECK(!gm_pattern_matches((const uint8_t *)"[", 2, &pattern));
}

static void tiny_compare(void) {
    uint8_t *data = malloc(1), *query = malloc(1);
    CHECK(data != NULL && query != NULL);
    *data = 'F'; *query = 'f';
    const gm_pattern pattern = {query, 1, GM_MORPH_HEX_TEXT, GM_COMPARE_HEX_TEXT};
    CHECK(gm_pattern_matches(data, 1, &pattern));
    CHECK(!gm_pattern_matches(data, 0, &pattern));
    *data = 0xff;
    CHECK(!gm_pattern_matches(data, 1, &pattern));
    free(data); free(query);
}

static void check_numeric(uint64_t value, unsigned int selection) {
    char decimal[32], hexadecimal[32];
    CHECK(snprintf(decimal, sizeof(decimal), "%" PRIu64, value) > 0);
    CHECK(snprintf(hexadecimal, sizeof(hexadecimal), "0X%" PRIX64, value) > 0);
    gm_morph_set set = generate(decimal, selection);
    gm_morph_set hex = generate(hexadecimal, selection);
    size_t index = 0;
    for(size_t kind = 0; kind < sizeof(widths) / sizeof(widths[0]); ++kind) {
        if((selection & (1u << numeric_kinds[kind])) == 0) continue;
        const gm_pattern *pattern = &set.patterns[index];
        CHECK(pattern->morph == numeric_kinds[kind] && pattern->length == widths[kind]);
        CHECK(pattern->comparison == GM_COMPARE_EXACT);
        CHECK(hex.patterns[index].length == widths[kind]);
        CHECK(memcmp(pattern->bytes, hex.patterns[index].bytes, widths[kind]) == 0);
        uint64_t rest = value;
        for(size_t i = 0; i < widths[kind]; ++i) {
            const bool big = kind != 0 && kind % 2 == 0;
            const size_t position = big ? widths[kind] - i - 1 : i;
            CHECK(pattern->bytes[position] == rest % UINT64_C(256));
            rest /= UINT64_C(256);
        }
        CHECK(rest == 0);
        ++index;
    }
    CHECK(set.count == index && hex.count == index);
    gm_morph_set_free(&set); gm_morph_set_free(&hex);
}

static void integer_widths(void) {
    check_numeric(0, GM_SELECT_UINT);
    check_numeric(255, GM_SELECT_UINT);
    check_numeric(12000, GM_SELECT_UINT & ~GM_SELECT_UINT8);
    check_numeric(65535, GM_SELECT_UINT16_LE | GM_SELECT_UINT16_BE);
    check_numeric(UINT32_MAX, GM_SELECT_UINT32_LE | GM_SELECT_UINT32_BE);
    check_numeric(UINT64_MAX, GM_SELECT_UINT64_LE | GM_SELECT_UINT64_BE);
    check_numeric(UINT64_C(0x0123456789abcdef), GM_SELECT_UINT64_LE | GM_SELECT_UINT64_BE);
    gm_morph_set zeroes = generate("000012000", GM_SELECT_UINT16_LE);
    CHECK(zeroes.patterns[0].bytes[0] == 0xe0 && zeroes.patterns[0].bytes[1] == 0x2e);
    gm_morph_set_free(&zeroes);
}

static void integer_ranges(void) {
    const char *queries[] = {"256", "65536", "4294967296", "18446744073709551616", "0x10000000000000000"};
    const unsigned int selections[] = {GM_SELECT_UINT8, GM_SELECT_UINT16_BE, GM_SELECT_UINT32_LE,
                                      GM_SELECT_UINT64_LE, GM_SELECT_UINT64_BE};
    for(size_t i = 0; i < sizeof(selections) / sizeof(selections[0]); ++i) {
        gm_morph_set set = {0};
        CHECK(gm_morph_generate((const uint8_t *)queries[i], strlen(queries[i]), selections[i], &set, NULL) == GM_MORPH_INTEGER_RANGE);
        CHECK(set.storage == NULL && set.count == 0);
    }
    gm_morph_set set = {0};
    CHECK(gm_morph_generate((const uint8_t *)"65536", 5, GM_SELECT_RAW | GM_SELECT_UINT16_LE | GM_SELECT_UINT64_BE, &set, NULL) == GM_MORPH_INTEGER_RANGE);
    CHECK(set.storage == NULL && set.count == 0);
}

static void integer_syntax(void) {
    const char *queries[] = {"-1", "+1", " 1", "1 ", "1\t", "1.0", "1e3", "0b10", "0o10", "1_000", "ff", "0x", "0xg", "0x1g"};
    const size_t offsets[] = {0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 0, 2, 2, 3};
    for(size_t i = 0; i < sizeof(offsets) / sizeof(offsets[0]); ++i) {
        gm_morph_set set = {0};
        size_t error = SIZE_MAX;
        CHECK(gm_morph_generate((const uint8_t *)queries[i], strlen(queries[i]), GM_SELECT_UINT64_LE, &set, &error) == GM_MORPH_INVALID_INTEGER);
        CHECK(error == offsets[i] && set.storage == NULL && set.count == 0);
    }
    const uint8_t embedded[] = {'1', 0, '2'};
    gm_morph_set set = {0};
    size_t error = 0;
    CHECK(gm_morph_generate(embedded, sizeof(embedded), GM_SELECT_UINT8, &set, &error) == GM_MORPH_INVALID_INTEGER && error == 1);
    const uint8_t invalid = 0xff;
    CHECK(gm_morph_generate(&invalid, 1, GM_SELECT_UINT8, &set, &error) == GM_MORPH_INVALID_INTEGER && error == 0);
    const uint8_t one = '1';
    CHECK(gm_morph_generate(&one, 1, GM_SELECT_UINT8, &set, NULL) == GM_MORPH_OK);
    CHECK(set.patterns[0].bytes[0] == 1);
    gm_morph_set_free(&set);
}

static void integer_exhaustive(void) {
    for(uint64_t value = 0; value <= UINT16_MAX; ++value) {
        check_numeric(value, GM_SELECT_UINT16_LE | GM_SELECT_UINT16_BE);
    }
}

static void selection_and_ownership(void) {
    const unsigned int bits[] = {GM_SELECT_RAW, GM_SELECT_UTF8, GM_SELECT_UTF16_LE, GM_SELECT_UTF16_BE,
        GM_SELECT_HEX_TEXT, GM_SELECT_UINT8, GM_SELECT_UINT16_LE, GM_SELECT_UINT16_BE,
        GM_SELECT_UINT32_LE, GM_SELECT_UINT32_BE, GM_SELECT_UINT64_LE, GM_SELECT_UINT64_BE};
    for(unsigned int mask = 1; mask < (1u << GM_MAX_QUERY_MORPHS); ++mask) {
        unsigned int selection = 0;
        size_t expected_count = 0;
        for(size_t i = 0; i < GM_MAX_QUERY_MORPHS; ++i) {
            if((mask & (1u << i)) != 0) { selection |= bits[i]; ++expected_count; }
        }
        gm_morph_set set = generate("127", selection);
        CHECK(set.count == expected_count);
        unsigned int seen = 0;
        for(size_t i = 0; i < set.count; ++i) {
            CHECK(i == 0 || set.patterns[i - 1].morph < set.patterns[i].morph);
            CHECK(set.patterns[i].length != 0 && set.patterns[i].bytes != NULL);
            seen |= 1u << set.patterns[i].morph;
        }
        CHECK(seen == selection);
        gm_morph_set_free(&set);
    }
    uint8_t query[] = {'1', '2', '7'};
    gm_morph_set set = {0};
    CHECK(gm_morph_generate(query, sizeof(query), GM_SELECT_ALL, &set, NULL) == GM_MORPH_OK);
    memset(query, 'x', sizeof(query));
    CHECK(set.count == GM_MAX_QUERY_MORPHS);
    CHECK(memcmp(set.patterns[0].bytes, "127", 3) == 0);
    CHECK(memcmp(set.patterns[4].bytes, "313237", 6) == 0);
    CHECK(set.patterns[5].bytes[0] == 127);
    gm_morph_set_free(&set);
    gm_morph_set_free(&set);
    CHECK(gm_morph_generate(query, sizeof(query), 1u << GM_MORPH_UINT_LE, &set, NULL) == GM_MORPH_INVALID_ARGUMENT);
    CHECK(gm_morph_generate(query, sizeof(query), 1u << GM_MORPH_UINT_BE, &set, NULL) == GM_MORPH_INVALID_ARGUMENT);
}

typedef struct { gm_match matches[4096]; size_t count; } collection;
static void collect(const gm_match *match, void *context) {
    collection *result = context;
    CHECK(result->count < sizeof(result->matches) / sizeof(result->matches[0]));
    result->matches[result->count++] = *match;
}
static FILE *make_stream(const uint8_t *data, size_t length) {
    FILE *stream = NULL;
#ifdef _MSC_VER
    CHECK(fopen_s(&stream, fixture_path, "w+b") == 0);
#else
    stream = fopen(fixture_path, "w+b");
#endif
    CHECK(stream != NULL);
    CHECK(fwrite(data, 1, length, stream) == length);
    CHECK(fseek(stream, 0, SEEK_SET) == 0);
    return stream;
}
/* Independent reference normalises lower-case a-f upwards, unlike the engine. */
static bool reference_equal(const uint8_t *data, const gm_pattern *pattern) {
    for(size_t i = 0; i < pattern->length; ++i) {
        unsigned int a = data[i], b = pattern->bytes[i];
        if(pattern->comparison == GM_COMPARE_HEX_TEXT) {
            if(a >= 'a' && a <= 'f') a -= 32;
            if(b >= 'a' && b <= 'f') b -= 32;
        }
        if(a != b) return false;
    }
    return true;
}
static void verify_stream(const uint8_t *data, size_t length, const gm_morph_set *set, size_t chunk) {
    FILE *stream = make_stream(data, length);
    collection result = {0};
    uint64_t count = 0;
    const uint64_t base = UINT64_C(0x100000000);
    CHECK(gm_search_stream_patterns(stream, set->patterns, set->count, chunk, base,
                                   collect, &result, &count) == GM_SCAN_OK);
    size_t expected = 0;
    for(size_t offset = 0; offset < length; ++offset) {
        for(size_t i = 0; i < set->count; ++i) {
            const gm_pattern *p = &set->patterns[i];
            if(p->length > length - offset || !reference_equal(data + offset, p)) continue;
            CHECK(expected < result.count);
            CHECK(result.matches[expected].offset == base + offset);
            CHECK(result.matches[expected].morph == p->morph);
            CHECK(result.matches[expected].length == p->length);
            ++expected;
        }
    }
    CHECK(count == expected && result.count == expected);
    CHECK(fclose(stream) == 0);
    CHECK(remove(fixture_path) == 0);
}
static void stream_hex(void) {
    const uint8_t query = 0xaa;
    gm_morph_set set = {0};
    CHECK(gm_morph_generate(&query, 1, GM_SELECT_RAW | GM_SELECT_HEX_TEXT, &set, NULL) == GM_MORPH_OK);
    for(size_t prefix = 0; prefix < 20; ++prefix) {
        uint8_t data[32];
        memset(data, '|', sizeof(data));
        const uint8_t suffix[] = {'a', 'A', 'A', 'a', 'a', 0xaa, 'A'};
        memcpy(data + prefix, suffix, sizeof(suffix));
        for(size_t chunk = 1; chunk < 14; ++chunk) verify_stream(data, prefix + sizeof(suffix), &set, chunk);
    }
    gm_morph_set_free(&set);
}
static void stream_integers(void) {
    gm_morph_set set = generate("0", GM_SELECT_ALL);
    uint8_t zeros[32] = {0};
    for(size_t length = 0; length <= sizeof(zeros); ++length) {
        for(size_t chunk = 1; chunk <= 9; ++chunk) verify_stream(zeros, length, &set, chunk);
    }
    gm_morph_set_free(&set);
    set = generate("127", GM_SELECT_ALL);
    uint8_t data[256];
    memset(data, '|', sizeof(data));
    for(size_t i = 0; i < set.count; ++i) {
        memcpy(data + 1 + i * 16, set.patterns[i].bytes, set.patterns[i].length);
    }
    for(size_t chunk = 1; chunk <= 25; ++chunk) verify_stream(data, sizeof(data), &set, chunk);
    gm_morph_set_free(&set);
}
static void stream_invalid_rule(void) {
    const uint8_t byte = 'a';
    FILE *stream = make_stream(&byte, 1);
    const gm_pattern pattern = {&byte, 1, GM_MORPH_RAW, (gm_comparison)99};
    uint64_t count = 99;
    CHECK(gm_search_stream_patterns(stream, &pattern, 1, 1, 0, NULL, NULL, &count) == GM_SCAN_INVALID_ARGUMENT);
    CHECK(count == 0 && ftell(stream) == 0);
    CHECK(fclose(stream) == 0 && remove(fixture_path) == 0);
}

int main(int argc, char **argv) {
    CHECK(argc == 3);
    fixture_path = argv[2];
    const char *name = argv[1];
    if(strcmp(name, "hex_bytes") == 0) hex_bytes();
    else if(strcmp(name, "hex_all_bytes") == 0) hex_all_bytes();
    else if(strcmp(name, "hex_overflow") == 0) hex_overflow();
    else if(strcmp(name, "compare_rules") == 0) compare_rules();
    else if(strcmp(name, "tiny_compare") == 0) tiny_compare();
    else if(strcmp(name, "integer_widths") == 0) integer_widths();
    else if(strcmp(name, "integer_ranges") == 0) integer_ranges();
    else if(strcmp(name, "integer_syntax") == 0) integer_syntax();
    else if(strcmp(name, "integer_exhaustive") == 0) integer_exhaustive();
    else if(strcmp(name, "selection_and_ownership") == 0) selection_and_ownership();
    else if(strcmp(name, "stream_hex") == 0) stream_hex();
    else if(strcmp(name, "stream_integers") == 0) stream_integers();
    else if(strcmp(name, "stream_invalid_rule") == 0) stream_invalid_rule();
    else CHECK(false);
    return 0;
}
