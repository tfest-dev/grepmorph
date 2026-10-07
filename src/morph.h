#ifndef GREPMORPH_MORPH_H
#define GREPMORPH_MORPH_H

#include "grepmorph.h"

#define GM_SELECT_RAW      (1u << GM_MORPH_RAW)
#define GM_SELECT_UTF8     (1u << GM_MORPH_UTF8)
#define GM_SELECT_UTF16_LE (1u << GM_MORPH_UTF16_LE)
#define GM_SELECT_UTF16_BE (1u << GM_MORPH_UTF16_BE)
#define GM_SELECT_TEXT     (GM_SELECT_UTF8 | GM_SELECT_UTF16_LE | GM_SELECT_UTF16_BE)
#define GM_SELECT_ALL      (GM_SELECT_RAW | GM_SELECT_TEXT)
#define GM_MAX_QUERY_MORPHS 4

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
    GM_MORPH_NO_MEMORY
} gm_morph_status;

/*
 * Compile once, then reuse across files. query is a valid byte range, not a
 * NUL-terminated string. RAW accepts arbitrary bytes. Any text selection
 * requires well-formed UTF-8, including when RAW is also selected.
 *
 * out must not own an earlier result: initialise to zero or release it first.
 * Failure leaves out empty; invalid UTF-8 sets error_offset to the start of
 * the ill-formed sequence. error_offset is optional and otherwise set to zero.
 * The result owns its storage; no reference to query is retained. Patterns
 * are in raw/utf8/utf16-le/utf16-be order. No BOM, terminator, normalisation
 * or case folding is added. The caller releases successful results.
 */
gm_morph_status gm_morph_generate(
    const uint8_t *query, size_t length, unsigned int selection,
    gm_morph_set *out, size_t *error_offset
);
void gm_morph_set_free(gm_morph_set *set);
const char *gm_morph_status_name(gm_morph_status status);

#endif
