#include "grepmorph.h"

#include <string.h>

size_t gm_search_exact(
    const uint8_t *haystack,
    size_t haystack_length,
    const uint8_t *needle,
    size_t needle_length,
    gm_morph morph,
    gm_match_sink sink,
    void *context
) {
    size_t count = 0;

    if(haystack == NULL || needle == NULL || needle_length == 0 || needle_length > haystack_length) {
        return 0;
    }

    const size_t last = haystack_length - needle_length;
    for(size_t offset = 0; offset <= last; ++offset) {
        if(memcmp(haystack + offset, needle, needle_length) != 0) {
            continue;
        }

        const gm_match match = {
            .offset = (uint64_t)offset,
            .length = needle_length,
            .morph = morph,
        };

        if(sink != NULL) {
            sink(&match, context);
        }
        ++count;
    }

    return count;
}

static uint8_t fold_hex(uint8_t byte) {
    return byte >= 'A' && byte <= 'F' ? (uint8_t)(byte + ('a' - 'A')) : byte;
}

bool gm_pattern_matches(const uint8_t *data, size_t length, const gm_pattern *pattern) {
    if(data == NULL || pattern == NULL || pattern->bytes == NULL ||
       pattern->length == 0 || pattern->length > length) return false;
    if(pattern->comparison == GM_COMPARE_EXACT) {
        return memcmp(data, pattern->bytes, pattern->length) == 0;
    }
    if(pattern->comparison != GM_COMPARE_HEX_TEXT) return false;
    for(size_t i = 0; i < pattern->length; ++i) {
        if(fold_hex(data[i]) != fold_hex(pattern->bytes[i])) return false;
    }
    return true;
}
