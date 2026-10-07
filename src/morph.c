#include "morph.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

const char *gm_morph_name(gm_morph morph) {
    switch(morph) {
        case GM_MORPH_RAW:            return "raw";
        case GM_MORPH_UTF8:           return "utf8";
        case GM_MORPH_UTF16_LE:       return "utf16-le";
        case GM_MORPH_UTF16_BE:       return "utf16-be";
        case GM_MORPH_HEX_TEXT:       return "hex-text";
        case GM_MORPH_BASE64_DECODED: return "base64-decoded";
        case GM_MORPH_UINT_LE:        return "uint-le";
        case GM_MORPH_UINT_BE:        return "uint-be";
        default:                      return "unknown";
    }
}

/* Decode one scalar, accepting only shortest-form UTF-8, never surrogates.
 * Bounds are checked before reading continuation bytes. */
static bool decode_scalar(const uint8_t *data, size_t remaining,
                          uint32_t *scalar, size_t *used) {
    if(remaining == 0) return false;
    const uint8_t first = data[0];
    uint32_t value;
    uint32_t minimum;
    size_t needed;
    if(first < 0x80) {
        *scalar = first;
        *used = 1;
        return true;
    }
    if(first >= 0xc2 && first <= 0xdf) {
        needed = 2; minimum = 0x80; value = first & 0x1fu;
    } else if(first >= 0xe0 && first <= 0xef) {
        needed = 3; minimum = 0x800; value = first & 0x0fu;
    } else if(first >= 0xf0 && first <= 0xf4) {
        needed = 4; minimum = 0x10000; value = first & 0x07u;
    } else {
        return false;
    }
    if(needed > remaining) return false;
    for(size_t i = 1; i < needed; ++i) {
        if((data[i] & 0xc0u) != 0x80u) return false;
        value = (value << 6) | (data[i] & 0x3fu);
    }
    if(value < minimum || value > 0x10ffff ||
       (value >= 0xd800 && value <= 0xdfff)) return false;
    *scalar = value;
    *used = needed;
    return true;
}

static void write_unit(uint8_t *le, uint8_t *be, size_t offset, uint16_t unit) {
    if(le != NULL) {
        le[offset] = (uint8_t)(unit & 0xffu);
        le[offset + 1] = (uint8_t)(unit >> 8);
    }
    if(be != NULL) {
        be[offset] = (uint8_t)(unit >> 8);
        be[offset + 1] = (uint8_t)(unit & 0xffu);
    }
}

void gm_morph_set_free(gm_morph_set *set) {
    if(set == NULL) return;
    free(set->storage);
    *set = (gm_morph_set){0};
}

gm_morph_status gm_morph_generate(
    const uint8_t *query, size_t length, unsigned int selection,
    gm_morph_set *out, size_t *error_offset
) {
    if(error_offset != NULL) *error_offset = 0;
    if(out == NULL) return GM_MORPH_INVALID_ARGUMENT;
    *out = (gm_morph_set){0};
    if(query == NULL || length == 0 || selection == 0 ||
       (selection & ~GM_SELECT_ALL) != 0) return GM_MORPH_INVALID_ARGUMENT;

    size_t wide_length = 0;
    if((selection & GM_SELECT_TEXT) != 0) {
        for(size_t offset = 0; offset < length;) {
            uint32_t scalar;
            size_t used;
            if(!decode_scalar(query + offset, length - offset, &scalar, &used)) {
                if(error_offset != NULL) *error_offset = offset;
                return GM_MORPH_INVALID_UTF8;
            }
            const size_t added = scalar > 0xffff ? 4u : 2u;
            if(wide_length > SIZE_MAX - added) return GM_MORPH_SIZE_OVERFLOW;
            wide_length += added;
            offset += used;
        }
    }
    size_t capacity = (selection & (GM_SELECT_RAW | GM_SELECT_UTF8)) != 0 ? length : 0;
    for(unsigned int bit = GM_SELECT_UTF16_LE; bit <= GM_SELECT_UTF16_BE; bit <<= 1) {
        if((selection & bit) != 0) {
            if(wide_length > SIZE_MAX - capacity) return GM_MORPH_SIZE_OVERFLOW;
            capacity += wide_length;
        }
    }
    uint8_t *storage = malloc(capacity);
    if(storage == NULL) return GM_MORPH_NO_MEMORY;
    gm_morph_set result = {.storage = storage};
    size_t written = 0;
    if((selection & (GM_SELECT_RAW | GM_SELECT_UTF8)) != 0) {
        memcpy(storage, query, length);
        written = length;
        if((selection & GM_SELECT_RAW) != 0) {
            result.patterns[result.count++] = (gm_pattern){storage, length, GM_MORPH_RAW};
        }
        if((selection & GM_SELECT_UTF8) != 0) {
            result.patterns[result.count++] = (gm_pattern){storage, length, GM_MORPH_UTF8};
        }
    }
    uint8_t *le = NULL;
    uint8_t *be = NULL;
    if((selection & GM_SELECT_UTF16_LE) != 0) {
        le = storage + written;
        result.patterns[result.count++] = (gm_pattern){le, wide_length, GM_MORPH_UTF16_LE};
        written += wide_length;
    }
    if((selection & GM_SELECT_UTF16_BE) != 0) {
        be = storage + written;
        result.patterns[result.count++] = (gm_pattern){be, wide_length, GM_MORPH_UTF16_BE};
    }
    if(le != NULL || be != NULL) {
        size_t wide_offset = 0;
        for(size_t offset = 0; offset < length;) {
            uint32_t scalar;
            size_t used;
            if(!decode_scalar(query + offset, length - offset, &scalar, &used)) {
                if(error_offset != NULL) *error_offset = offset;
                gm_morph_set_free(&result);
                return GM_MORPH_INVALID_UTF8;
            }
            if(scalar > 0xffff) {
                scalar -= 0x10000;
                write_unit(le, be, wide_offset, (uint16_t)(0xd800u + (scalar >> 10)));
                wide_offset += 2;
                write_unit(le, be, wide_offset, (uint16_t)(0xdc00u + (scalar & 0x3ffu)));
            } else {
                write_unit(le, be, wide_offset, (uint16_t)scalar);
            }
            wide_offset += 2;
            offset += used;
        }
    }
    *out = result;
    return GM_MORPH_OK;
}

const char *gm_morph_status_name(gm_morph_status status) {
    switch(status) {
        case GM_MORPH_OK:               return "ok";
        case GM_MORPH_INVALID_ARGUMENT: return "invalid query or morph selection";
        case GM_MORPH_INVALID_UTF8:     return "invalid UTF-8 query";
        case GM_MORPH_SIZE_OVERFLOW:    return "query morph size overflow";
        case GM_MORPH_NO_MEMORY:        return "cannot allocate query morphs";
        default:                       return "unknown morph error";
    }
}
