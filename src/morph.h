#ifndef GREPMORPH_MORPH_H
#define GREPMORPH_MORPH_H

#include "grepmorph.h"

#define GM_SELECT_RAW      (1u << GM_MORPH_RAW)
#define GM_SELECT_UTF8     (1u << GM_MORPH_UTF8)
#define GM_SELECT_UTF16_LE (1u << GM_MORPH_UTF16_LE)
#define GM_SELECT_UTF16_BE (1u << GM_MORPH_UTF16_BE)
#define GM_SELECT_TEXT     (GM_SELECT_UTF8 | GM_SELECT_UTF16_LE | GM_SELECT_UTF16_BE)
#define GM_SELECT_HEX_TEXT  (1u << GM_MORPH_HEX_TEXT)
#define GM_SELECT_UINT8    (1u << GM_MORPH_UINT8)
#define GM_SELECT_UINT16_LE (1u << GM_MORPH_UINT16_LE)
#define GM_SELECT_UINT16_BE (1u << GM_MORPH_UINT16_BE)
#define GM_SELECT_UINT32_LE (1u << GM_MORPH_UINT32_LE)
#define GM_SELECT_UINT32_BE (1u << GM_MORPH_UINT32_BE)
#define GM_SELECT_UINT64_LE (1u << GM_MORPH_UINT64_LE)
#define GM_SELECT_UINT64_BE (1u << GM_MORPH_UINT64_BE)
#define GM_SELECT_UINT (GM_SELECT_UINT8 | GM_SELECT_UINT16_LE | GM_SELECT_UINT16_BE | \
                        GM_SELECT_UINT32_LE | GM_SELECT_UINT32_BE | \
                        GM_SELECT_UINT64_LE | GM_SELECT_UINT64_BE)
#define GM_SELECT_ALL (GM_SELECT_RAW | GM_SELECT_TEXT | GM_SELECT_HEX_TEXT | GM_SELECT_UINT)
#define GM_MAX_QUERY_MORPHS 12

typedef struct {
    gm_pattern patterns[GM_MAX_QUERY_MORPHS];
    size_t count;
    uint8_t *storage;
} gm_morph_set;

typedef enum {
    GM_MORPH_OK = 0,
    GM_MORPH_INVALID_ARGUMENT,
    GM_MORPH_INVALID_UTF8,
    GM_MORPH_SIZE_OVERFLOW,
    GM_MORPH_NO_MEMORY,
    GM_MORPH_INVALID_INTEGER,
    GM_MORPH_INTEGER_RANGE
} gm_morph_status;

/*
 * Compile once, then reuse across files. query is a valid byte range, not a
 * NUL-terminated string. RAW accepts arbitrary bytes. Any text selection
 * requires well-formed UTF-8, including when RAW is also selected. HEX_TEXT
 * encodes the query bytes as contiguous ASCII hex, with A-F-only folding.
 * Integer selections parse the entire query as unsigned decimal or 0x/0X hex.
 * No signs, whitespace, separators, implicit octal, or trailing data are allowed.
 * The value must fit EVERY selected width; no truncation or silent omission.
 *
 * out must not own an earlier result: initialise to zero or release it first.
 * Failure leaves out empty; invalid UTF-8 sets error_offset to the start of
 * the ill-formed sequence; invalid integers set it to the offending byte
 * (or the end of a bare 0x prefix). error_offset is optional; other errors
 * leave it at zero.
 * The result owns its storage; no reference to query is retained. Patterns
 * are ordered raw/utf8/utf16-le/utf16-be/hex-text/uint8, then increasing integer
 * width (LE before BE). No BOM, terminator or normalisation is added. Only the
 * hex-text pattern uses folding. Other patterns remain byte-exact.
 * The caller releases successful results.
 */
gm_morph_status gm_morph_generate(
    const uint8_t *query, size_t length, unsigned int selection,
    gm_morph_set *out, size_t *error_offset
);
void gm_morph_set_free(gm_morph_set *set);
const char *gm_morph_status_name(gm_morph_status status);

#endif
