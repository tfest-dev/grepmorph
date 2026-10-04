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
