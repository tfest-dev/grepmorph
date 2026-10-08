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
        case GM_MORPH_UINT8: return "uint8";
        case GM_MORPH_UINT16_LE: return "uint16-le";
        case GM_MORPH_UINT16_BE: return "uint16-be";
        case GM_MORPH_UINT32_LE: return "uint32-le";
        case GM_MORPH_UINT32_BE: return "uint32-be";
        case GM_MORPH_UINT64_LE: return "uint64-le";
        case GM_MORPH_UINT64_BE: return "uint64-be";
        default:                      return "unknown";
    }
}

typedef struct {
    gm_morph morph;
    size_t width;
    bool big_endian;
} integer_spec;

static const integer_spec integers[] = {
    {GM_MORPH_UINT8, 1, false},
    {GM_MORPH_UINT16_LE, 2, false}, {GM_MORPH_UINT16_BE, 2, true},
    {GM_MORPH_UINT32_LE, 4, false}, {GM_MORPH_UINT32_BE, 4, true},
    {GM_MORPH_UINT64_LE, 8, false}, {GM_MORPH_UINT64_BE, 8, true},
};

static gm_morph_status parse_integer(const uint8_t *query, size_t length,
                                    uint64_t *value, size_t *error_offset) {
    size_t offset = 0;
    unsigned int base = 10;
    if(length >= 2 && query[0] == '0' && (query[1] == 'x' || query[1] == 'X')) {
        offset = 2;
        base = 16;
    }
    if(offset == length) {
        if(error_offset != NULL) *error_offset = offset;
        return GM_MORPH_INVALID_INTEGER;
    }
    uint64_t result = 0;
    for(; offset < length; ++offset) {
        const uint8_t byte = query[offset];
        unsigned int digit = 16;
        if(byte >= '0' && byte <= '9') digit = (unsigned int)(byte - '0');
        else if(byte >= 'a' && byte <= 'f') digit = (unsigned int)(byte - 'a') + 10u;
        else if(byte >= 'A' && byte <= 'F') digit = (unsigned int)(byte - 'A') + 10u;
        if(digit >= base) {
            if(error_offset != NULL) *error_offset = offset;
            return GM_MORPH_INVALID_INTEGER;
        }
        if(result > (UINT64_MAX - digit) / base) return GM_MORPH_INTEGER_RANGE;
        result = result * base + digit;
    }
    *value = result;
    return GM_MORPH_OK;
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

    size_t hex_length = 0;
    if((selection & GM_SELECT_HEX_TEXT) != 0) {
        if(length > SIZE_MAX / 2) return GM_MORPH_SIZE_OVERFLOW;
        hex_length = length * 2;
    }
    uint64_t integer = 0;
    if((selection & GM_SELECT_UINT) != 0) {
        const gm_morph_status parsed = parse_integer(query, length, &integer, error_offset);
        if(parsed != GM_MORPH_OK) return parsed;
        for(size_t i = 0; i < sizeof(integers) / sizeof(integers[0]); ++i) {
            const integer_spec *spec = &integers[i];
            if((selection & (1u << spec->morph)) != 0 && spec->width < 8 &&
               integer >= (UINT64_C(1) << (spec->width * 8))) return GM_MORPH_INTEGER_RANGE;
        }
    }
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
    if(hex_length > SIZE_MAX - capacity) return GM_MORPH_SIZE_OVERFLOW;
    capacity += hex_length;
    for(size_t i = 0; i < sizeof(integers) / sizeof(integers[0]); ++i) {
        if((selection & (1u << integers[i].morph)) != 0) {
            if(integers[i].width > SIZE_MAX - capacity) return GM_MORPH_SIZE_OVERFLOW;
            capacity += integers[i].width;
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
            result.patterns[result.count++] = (gm_pattern){storage, length, GM_MORPH_RAW, GM_COMPARE_EXACT};
        }
        if((selection & GM_SELECT_UTF8) != 0) {
            result.patterns[result.count++] = (gm_pattern){storage, length, GM_MORPH_UTF8, GM_COMPARE_EXACT};
        }
    }
    uint8_t *le = NULL;
    uint8_t *be = NULL;
    if((selection & GM_SELECT_UTF16_LE) != 0) {
        le = storage + written;
        result.patterns[result.count++] = (gm_pattern){le, wide_length, GM_MORPH_UTF16_LE, GM_COMPARE_EXACT};
        written += wide_length;
    }
    if((selection & GM_SELECT_UTF16_BE) != 0) {
        be = storage + written;
        result.patterns[result.count++] = (gm_pattern){be, wide_length, GM_MORPH_UTF16_BE, GM_COMPARE_EXACT};
        written += wide_length;
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
    if(hex_length != 0) {
        static const uint8_t digits[] = "0123456789abcdef";
        uint8_t *bytes = storage + written;
        for(size_t i = 0; i < length; ++i) {
            bytes[i * 2] = digits[query[i] >> 4];
            bytes[i * 2 + 1] = digits[query[i] & 0x0fu];
        }
        result.patterns[result.count++] = (gm_pattern){
            bytes, hex_length, GM_MORPH_HEX_TEXT, GM_COMPARE_HEX_TEXT
        };
        written += hex_length;
    }
    for(size_t i = 0; i < sizeof(integers) / sizeof(integers[0]); ++i) {
        const integer_spec *spec = &integers[i];
        if((selection & (1u << spec->morph)) == 0) continue;
        uint8_t *bytes = storage + written;
        for(size_t byte = 0; byte < spec->width; ++byte) {
            const size_t index = spec->big_endian ? spec->width - byte - 1 : byte;
            bytes[index] = (uint8_t)((integer >> (byte * 8)) & UINT64_C(0xff));
        }
        result.patterns[result.count++] = (gm_pattern){
            bytes, spec->width, spec->morph, GM_COMPARE_EXACT
        };
        written += spec->width;
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
        case GM_MORPH_INVALID_INTEGER: return "invalid unsigned integer query";
        case GM_MORPH_INTEGER_RANGE: return "integer value does not fit every selected width (maximum 64 bits)";
        default:                       return "unknown morph error";
    }
}
