#include "morph.h"
#include "test_check.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

static void expect(const uint8_t *query, size_t length,
                   const uint8_t *le, size_t wide_length) {
    gm_morph_set set = {0};
    CHECK(gm_morph_generate(query, length, GM_SELECT_ALL, &set, NULL) == GM_MORPH_OK);
    CHECK(set.count == 4 && set.storage != NULL);
    for(size_t i = 0; i < 4; ++i) CHECK(set.patterns[i].morph == (gm_morph)i);
    CHECK(set.patterns[0].length == length && set.patterns[1].length == length);
    CHECK(memcmp(set.patterns[0].bytes, query, length) == 0);
    CHECK(memcmp(set.patterns[1].bytes, query, length) == 0);
    CHECK(set.patterns[2].length == wide_length && set.patterns[3].length == wide_length);
    CHECK(memcmp(set.patterns[2].bytes, le, wide_length) == 0);
    for(size_t i = 0; i < wide_length; i += 2) {
        CHECK(set.patterns[3].bytes[i] == le[i + 1]);
        CHECK(set.patterns[3].bytes[i + 1] == le[i]);
    }
    gm_morph_set_free(&set);
    CHECK(set.count == 0 && set.storage == NULL);
    gm_morph_set_free(&set);
}

static void ascii(void) {
    const uint8_t query[] = {'a', 'b', 'c'};
    const uint8_t le[] = {'a', 0, 'b', 0, 'c', 0};
    expect(query, sizeof(query), le, sizeof(le));
}

static void bmp(void) {
    /* U+00E9, U+20AC, U+4E2D: two- and three-byte UTF-8 sequences. */
    const uint8_t query[] = {0xc3, 0xa9, 0xe2, 0x82, 0xac, 0xe4, 0xb8, 0xad};
    const uint8_t le[] = {0xe9, 0x00, 0xac, 0x20, 0x2d, 0x4e};
    expect(query, sizeof(query), le, sizeof(le));
}

static void supplementary(void) {
    /* U+10000, U+1F600, U+10FFFF. */
    const uint8_t query[] = {0xf0, 0x90, 0x80, 0x80, 0xf0, 0x9f, 0x98, 0x80,
                             0xf4, 0x8f, 0xbf, 0xbf};
    const uint8_t le[] = {0x00, 0xd8, 0x00, 0xdc, 0x3d, 0xd8, 0x00, 0xde,
                          0xff, 0xdb, 0xff, 0xdf};
    expect(query, sizeof(query), le, sizeof(le));
}

static void binary_and_bom(void) {
    /* BOM supplied as a scalar is preserved, not silently stripped. */
    const uint8_t query[] = {0, 0xef, 0xbb, 0xbf, 'a', 0};
    const uint8_t le[] = {0, 0, 0xff, 0xfe, 'a', 0, 0, 0};
    expect(query, sizeof(query), le, sizeof(le));
    const uint8_t bytes[] = {0xff, 0x80, 0};
    gm_morph_set set = {0};
    CHECK(gm_morph_generate(bytes, sizeof(bytes), GM_SELECT_RAW, &set, NULL) == GM_MORPH_OK);
    CHECK(set.count == 1 && set.patterns[0].length == sizeof(bytes));
    CHECK(memcmp(bytes, set.patterns[0].bytes, sizeof(bytes)) == 0);
    gm_morph_set_free(&set);
}

static void reject(const uint8_t *data, size_t length, size_t offset) {
    /* Exact-sized allocations make tail overreads visible to ASan. */
    uint8_t *copy = malloc(length);
    CHECK(copy != NULL);
    memcpy(copy, data, length);
    gm_morph_set set = {0};
    size_t error = SIZE_MAX;
    CHECK(gm_morph_generate(copy, length, GM_SELECT_TEXT, &set, &error) == GM_MORPH_INVALID_UTF8);
    CHECK(error == offset && set.count == 0 && set.storage == NULL);
    free(copy);
}

static void truncated(void) {
    const uint8_t a[] = {0xc2};
    const uint8_t b[] = {0xe2, 0x82};
    const uint8_t c[] = {0xf0, 0x90, 0x80};
    const uint8_t d[] = {'x', 0xe2, 0x82};
    reject(a, sizeof(a), 0); reject(b, sizeof(b), 0);
    reject(c, sizeof(c), 0); reject(d, sizeof(d), 1);
}

static void overlong(void) {
    const uint8_t a[] = {0xc0, 0x80};
    const uint8_t b[] = {0xc1, 0xbf};
    const uint8_t c[] = {0xe0, 0x9f, 0xbf};
    const uint8_t d[] = {0xf0, 0x8f, 0xbf, 0xbf};
    reject(a, sizeof(a), 0); reject(b, sizeof(b), 0);
    reject(c, sizeof(c), 0); reject(d, sizeof(d), 0);
}

static void invalid_scalar(void) {
    const uint8_t a[] = {0xed, 0xa0, 0x80};
    const uint8_t b[] = {0xed, 0xbf, 0xbf};
    const uint8_t c[] = {0xf4, 0x90, 0x80, 0x80};
    const uint8_t d[] = {0xf5, 0x80, 0x80, 0x80};
    reject(a, sizeof(a), 0); reject(b, sizeof(b), 0);
    reject(c, sizeof(c), 0); reject(d, sizeof(d), 0);
}

static void invalid_continuation(void) {
    const uint8_t a[] = {0x80};
    const uint8_t b[] = {0xfe};
    const uint8_t c[] = {0xc2, 'a'};
    const uint8_t d[] = {0xe2, 0x82, 'a'};
    const uint8_t e[] = {0xf0, 0x90, 0x80, 0xff};
    reject(a, sizeof(a), 0); reject(b, sizeof(b), 0);
    reject(c, sizeof(c), 0); reject(d, sizeof(d), 0); reject(e, sizeof(e), 0);
}

static void selection_and_ownership(void) {
    uint8_t *query = malloc(1);
    CHECK(query != NULL);
    query[0] = 'a';
    gm_morph_set set = {0};
    for(unsigned int selection = 1; selection <= GM_SELECT_ALL; ++selection) {
        CHECK(gm_morph_generate(query, 1, selection, &set, NULL) == GM_MORPH_OK);
        size_t count = 0;
        for(unsigned int kind = 0; kind < GM_MAX_QUERY_MORPHS; ++kind) {
            if((selection & (1u << kind)) == 0) continue;
            CHECK(set.patterns[count].morph == (gm_morph)kind);
            CHECK(set.patterns[count].length == (kind < 2 ? 1u : 2u));
            ++count;
        }
        CHECK(set.count == count);
        gm_morph_set_free(&set);
    }
    CHECK(gm_morph_generate(query, 1, GM_SELECT_TEXT, &set, NULL) == GM_MORPH_OK);
    query[0] = 'x';
    free(query);
    CHECK(set.patterns[0].bytes[0] == 'a');
    CHECK(set.patterns[1].bytes[0] == 'a');
    CHECK(set.patterns[2].bytes[1] == 'a');
    gm_morph_set_free(&set);
}

static void invalid_arguments(void) {
    const uint8_t byte = 'a';
    gm_morph_set set = {0};
    CHECK(gm_morph_generate(NULL, 1, GM_SELECT_RAW, &set, NULL) == GM_MORPH_INVALID_ARGUMENT);
    CHECK(gm_morph_generate(&byte, 0, GM_SELECT_RAW, &set, NULL) == GM_MORPH_INVALID_ARGUMENT);
    CHECK(gm_morph_generate(&byte, 1, 0, &set, NULL) == GM_MORPH_INVALID_ARGUMENT);
    CHECK(gm_morph_generate(&byte, 1, GM_SELECT_ALL + 1u, &set, NULL) == GM_MORPH_INVALID_ARGUMENT);
    CHECK(gm_morph_generate(&byte, 1, GM_SELECT_ALL, NULL, NULL) == GM_MORPH_INVALID_ARGUMENT);
    const uint8_t invalid = 0xff;
    CHECK(gm_morph_generate(&invalid, 1, GM_SELECT_ALL, &set, NULL) == GM_MORPH_INVALID_UTF8);
    CHECK(set.storage == NULL && set.count == 0);
    gm_morph_set_free(NULL);
    for(int i = GM_MORPH_OK; i <= GM_MORPH_NO_MEMORY; ++i) {
        CHECK(strcmp(gm_morph_status_name((gm_morph_status)i), "unknown morph error") != 0);
    }
}

static void no_normalisation(void) {
    const uint8_t composed[] = {0xc3, 0xa9};
    const uint8_t decomposed[] = {'e', 0xcc, 0x81};
    gm_morph_set a = {0}, b = {0};
    CHECK(gm_morph_generate(composed, sizeof(composed), GM_SELECT_TEXT, &a, NULL) == GM_MORPH_OK);
    CHECK(gm_morph_generate(decomposed, sizeof(decomposed), GM_SELECT_TEXT, &b, NULL) == GM_MORPH_OK);
    CHECK(a.patterns[1].length == 2 && b.patterns[1].length == 4);
    CHECK(memcmp(a.patterns[1].bytes, b.patterns[1].bytes, 2) != 0);
    gm_morph_set_free(&a); gm_morph_set_free(&b);
}

/* Exhaust every scalar, including unassigned and noncharacter code points.
 * Unicode's UTF encoding rules do not depend on character-name databases. */
static void all_scalars(void) {
    for(uint32_t cp = 0; cp <= 0x10ffff; ++cp) {
        if(cp >= 0xd800 && cp <= 0xdfff) continue;
        uint8_t input[4], le[4];
        size_t n;
        if(cp < 0x80) { n = 1; input[0] = (uint8_t)cp; }
        else if(cp < 0x800) {
            n = 2;
            input[0] = (uint8_t)(0xc0u | (cp >> 6));
            input[1] = (uint8_t)(0x80u | (cp & 63u));
        } else if(cp < 0x10000) {
            n = 3;
            input[0] = (uint8_t)(0xe0u | (cp >> 12));
            input[1] = (uint8_t)(0x80u | ((cp >> 6) & 63u));
            input[2] = (uint8_t)(0x80u | (cp & 63u));
        } else {
            n = 4;
            input[0] = (uint8_t)(0xf0u | (cp >> 18));
            input[1] = (uint8_t)(0x80u | ((cp >> 12) & 63u));
            input[2] = (uint8_t)(0x80u | ((cp >> 6) & 63u));
            input[3] = (uint8_t)(0x80u | (cp & 63u));
        }
        size_t wide = 2;
        if(cp < 0x10000) {
            le[0] = (uint8_t)(cp & 255u);
            le[1] = (uint8_t)(cp >> 8);
        } else {
            const uint32_t high = 0xd800u + (cp - 0x10000u) / 1024u;
            const uint32_t low = 0xdc00u + (cp - 0x10000u) % 1024u;
            le[0] = (uint8_t)(high & 255u); le[1] = (uint8_t)(high / 256u);
            le[2] = (uint8_t)(low & 255u); le[3] = (uint8_t)(low / 256u);
            wide = 4;
        }
        expect(input, n, le, wide);
    }
}

int main(int argc, char **argv) {
    CHECK(argc == 2);
    const char *name = argv[1];
    if(strcmp(name, "ascii") == 0) ascii();
    else if(strcmp(name, "bmp") == 0) bmp();
    else if(strcmp(name, "supplementary") == 0) supplementary();
    else if(strcmp(name, "binary_and_bom") == 0) binary_and_bom();
    else if(strcmp(name, "truncated") == 0) truncated();
    else if(strcmp(name, "overlong") == 0) overlong();
    else if(strcmp(name, "invalid_scalar") == 0) invalid_scalar();
    else if(strcmp(name, "invalid_continuation") == 0) invalid_continuation();
    else if(strcmp(name, "selection_and_ownership") == 0) selection_and_ownership();
    else if(strcmp(name, "invalid_arguments") == 0) invalid_arguments();
    else if(strcmp(name, "no_normalisation") == 0) no_normalisation();
    else if(strcmp(name, "all_scalars") == 0) all_scalars();
    else CHECK(false);
    return 0;
}
