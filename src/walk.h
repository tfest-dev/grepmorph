#ifndef GREPMORPH_WALK_H
#define GREPMORPH_WALK_H

#include <stdbool.h>

/* Paths and messages are borrowed for the duration of each callback. */
typedef bool (*gm_file_visitor)(const char *path, bool explicit_input, void *context);
typedef void (*gm_path_error_sink)(const char *path, const char *message, void *context);

typedef enum {
    GM_WALK_OK = 0,
    GM_WALK_ERROR,
    GM_WALK_STOPPED
} gm_walk_status;

/*
 * Visit regular files under one command-line path. Directories require recursive.
 * Explicit paths follow platform link resolution; discovered links/reparse points
 * and special files are skipped. Directory entries are visited depth-first in
 * bytewise name order. No ignore rules or cross-input deduplication are applied.
 *
 * Returning false from visit stops traversal and releases queued paths. Errors
 * are reported and other entries continue where possible; ERROR means incomplete
 * discovery. A callback may report its own file errors without stopping the walk.
 * This path-based walk is not a snapshot or a hostile-filesystem security boundary.
 */
gm_walk_status gm_walk_path(
    const char *path,
    bool recursive,
    gm_file_visitor visit,
    gm_path_error_sink error,
    void *context
);

#endif
