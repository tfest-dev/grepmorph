#ifndef GREPMORPH_H
#define GREPMORPH_H

#include <stddef.h>
#include <stdint.h>

#define GREPMORPH_VERSION "0.1.0"

typedef enum {
    GM_MORPH_RAW = 0,
    GM_MORPH_UTF8,
    GM_MORPH_UTF16_LE,
    GM_MORPH_UTF16_BE,
    GM_MORPH_HEX_TEXT,
    GM_MORPH_BASE64_DECODED,
    GM_MORPH_UINT_LE,
    GM_MORPH_UINT_BE
} gm_morph;

/* A borrowed byte pattern. The owner keeps bytes alive during a search. */
typedef struct {
    const uint8_t *bytes;
    size_t length;
    gm_morph morph;
} gm_pattern;

typedef struct {
    uint64_t offset;
    size_t length;
    gm_morph morph;
} gm_match;

typedef void (*gm_match_sink)(const gm_match *match, void *context);

const char *gm_morph_name(gm_morph morph);

/*
 * Count exact byte matches, including overlaps. All supplied ranges must refer
 * to valid memory. NULL data, an empty needle, or a needle larger than the
 * haystack returns zero without reading either range. sink may be NULL.
 * No case folding or encoding conversion occurs here.
 */
size_t gm_search_exact(
    const uint8_t *haystack,
    size_t haystack_length,
    const uint8_t *needle,
    size_t needle_length,
    gm_morph morph,
    gm_match_sink sink,
    void *context
);

#endif
