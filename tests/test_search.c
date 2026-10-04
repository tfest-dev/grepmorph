#include "grepmorph.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef struct {
    uint64_t offsets[8];
    size_t count;
} collected_matches;

static void collect_match(const gm_match *match, void *context) {
    collected_matches *matches = context;
    assert(matches->count < (sizeof(matches->offsets) / sizeof(matches->offsets[0])));
    matches->offsets[matches->count++] = match->offset;
}

int main(void) {
    const uint8_t data[] = "abc--abc-ABC";
    const uint8_t needle[] = "abc";
    collected_matches matches = {0};

    const size_t count = gm_search_exact(
        data,
        strlen((const char *)data),
        needle,
        strlen((const char *)needle),
        GM_MORPH_RAW,
        collect_match,
        &matches
    );

    assert(count == 2);
    assert(matches.count == 2);
    assert(matches.offsets[0] == 0);
    assert(matches.offsets[1] == 5);
    assert(strcmp(gm_morph_name(GM_MORPH_UTF16_LE), "utf16-le") == 0);

    assert(gm_search_exact(data, sizeof(data), needle, 0, GM_MORPH_RAW, NULL, NULL) == 0);
    assert(gm_search_exact(data, 2, needle, 3, GM_MORPH_RAW, NULL, NULL) == 0);

    return 0;
}
