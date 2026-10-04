#ifndef GREPMORPH_FILE_SEARCH_H
#define GREPMORPH_FILE_SEARCH_H

#include "grepmorph.h"

#include <stdbool.h>
#include <stdio.h>

#define GM_DEFAULT_CHUNK_SIZE ((size_t)65536)

typedef enum {
    GM_SCAN_OK = 0,
    GM_SCAN_INVALID_ARGUMENT,
    GM_SCAN_NO_MEMORY,
    GM_SCAN_IO_ERROR,
    GM_SCAN_OFFSET_OVERFLOW
} gm_scan_status;

/*
 * Search from the stream's current position. base_offset is the absolute offset
 * assigned to its next byte; it does not seek. The caller owns the stream and
 * needle. The callback borrows each match for the duration of the call only.
 *
 * Memory use is chunk_size + needle_length - 1 bytes, independent of file size.
 * Overlapping and cross-chunk matches are each emitted once. A non-OK return
 * means results may be incomplete; match_count retains matches already emitted.
 * Empty or invalid patterns are errors, unlike the count-only in-memory helper.
 */
gm_scan_status gm_search_stream(
    FILE *stream,
    const uint8_t *needle,
    size_t needle_length,
    gm_morph morph,
    size_t chunk_size,
    uint64_t base_offset,
    gm_match_sink sink,
    void *context,
    uint64_t *match_count
);

const char *gm_scan_status_name(gm_scan_status status);

/* Open a regular file read-only in binary mode. NULL on error, with errno set. */
FILE *gm_open_regular_file(const char *path);

/* As above, but refuse a final symlink/reparse point when follow_links is false.
 * Ancestor components still follow platform resolution; this is not a sandbox. */
FILE *gm_open_regular_file_with_links(const char *path, bool follow_links);

#endif
