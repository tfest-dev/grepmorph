#include "walk.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#endif

typedef struct {
    char **items;
    size_t count;
    size_t capacity;
} path_list;

typedef struct {
    gm_path_error_sink error;
    void *context;
    bool failed;
} walk_state;

typedef enum { PATH_ERROR, PATH_SKIP, PATH_FILE, PATH_DIRECTORY } path_kind;

static void report(walk_state *state, const char *path, const char *message) {
    state->failed = true;
    if(state->error != NULL) state->error(path, message, state->context);
}

static void report_errno(walk_state *state, const char *path, int code) {
#ifdef _MSC_VER
    char message[256];
    if(strerror_s(message, sizeof(message), code) != 0) {
        report(state, path, "filesystem operation failed");
    } else {
        report(state, path, message);
    }
#else
    report(state, path, strerror(code));
#endif
}

#ifdef _WIN32
static void report_windows(walk_state *state, const char *path, DWORD code) {
    char message[512];
    DWORD length = FormatMessageA(
        FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL, code, 0, message, (DWORD)sizeof(message), NULL
    );
    if(length == 0) {
        (void)snprintf(message, sizeof(message), "Windows filesystem error %lu",
                       (unsigned long)code);
    } else {
        while(length > 0 && (message[length - 1] == '\r' || message[length - 1] == '\n')) {
            message[--length] = '\0';
        }
    }
    report(state, path, message);
}
#endif

static void clear_paths(path_list *list) {
    for(size_t i = 0; i < list->count; ++i) free(list->items[i]);
    free(list->items);
    *list = (path_list){0};
}

static bool reserve_paths(path_list *list, size_t extra) {
    const size_t maximum = SIZE_MAX / sizeof(*list->items);
    if(extra > maximum - list->count) return false;
    const size_t needed = list->count + extra;
    if(needed <= list->capacity) return true;
    size_t capacity = list->capacity == 0 ? 16 : list->capacity;
    while(capacity < needed) {
        if(capacity > maximum / 2) {
            capacity = needed;
            break;
        }
        capacity *= 2;
    }
    char **items = realloc(list->items, capacity * sizeof(*items));
    if(items == NULL) return false;
    list->items = items;
    list->capacity = capacity;
    return true;
}

/* Preserve spelling, including Windows drive-relative roots such as C:. */
static char *join_path(const char *parent, const char *name) {
    const size_t plen = strlen(parent);
    const size_t nlen = strlen(name);
    bool separator = plen > 0 && parent[plen - 1] != '/';
#ifdef _WIN32
    if(plen > 0 && parent[plen - 1] == '\\') separator = false;
    if(plen == 2 && parent[1] == ':') separator = false;
#endif
    const size_t extra = separator ? 2 : 1; /* Separator plus NUL. */
    if(plen > SIZE_MAX - extra || nlen > SIZE_MAX - extra - plen) return NULL;
    char *path = malloc(plen + nlen + extra);
    if(path == NULL) return NULL;
    memcpy(path, parent, plen);
    if(separator) path[plen] = '/';
    memcpy(path + plen + (separator ? 1 : 0), name, nlen + 1);
    return path;
}

static bool add_child(path_list *children, const char *parent, const char *name) {
    if(strcmp(name, ".") == 0 || strcmp(name, "..") == 0) return true;
    if(!reserve_paths(children, 1)) return false;
    char *path = join_path(parent, name);
    if(path == NULL) return false;
    children->items[children->count++] = path;
    return true;
}

static int compare_paths(const void *left, const void *right) {
    return strcmp(*(char *const *)left, *(char *const *)right);
}

static path_kind classify_path(const char *path, bool explicit_input, walk_state *state) {
#ifdef _WIN32
    const DWORD attributes = GetFileAttributesA(path);
    if(attributes == INVALID_FILE_ATTRIBUTES) {
        report_windows(state, path, GetLastError());
        return PATH_ERROR;
    }
    if(!explicit_input && (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
        return PATH_SKIP;
    }
    /* Directory attributes also handle roots/trailing separators without the
     * CRT's file-oriented path restrictions. Explicit directory links are
     * resolved by enumeration; a dangling target is then an input error. */
    if((attributes & FILE_ATTRIBUTE_DIRECTORY) != 0) return PATH_DIRECTORY;
    struct _stat64 metadata;
    if(_stat64(path, &metadata) != 0) {
        report_errno(state, path, errno);
        return PATH_ERROR;
    }
    if((metadata.st_mode & _S_IFMT) == _S_IFDIR) return PATH_DIRECTORY;
    if((metadata.st_mode & _S_IFMT) == _S_IFREG) return PATH_FILE;
#else
    struct stat metadata;
    if((explicit_input ? stat(path, &metadata) : lstat(path, &metadata)) != 0) {
        report_errno(state, path, errno);
        return PATH_ERROR;
    }
    if(S_ISDIR(metadata.st_mode)) return PATH_DIRECTORY;
    if(S_ISREG(metadata.st_mode)) return PATH_FILE;
#endif
    if(explicit_input) {
        report(state, path, "not a regular file or directory");
        return PATH_ERROR;
    }
    return PATH_SKIP;
}

/* Enumerate and close before descending: descriptor use does not grow with depth. */
static void read_directory(
    const char *path, bool explicit_input, path_list *children, walk_state *state
) {
#ifdef _WIN32
    (void)explicit_input;
    char *pattern = join_path(path, "*");
    if(pattern == NULL) {
        report(state, path, "cannot allocate directory search path");
        return;
    }
    WIN32_FIND_DATAA entry;
    const HANDLE handle = FindFirstFileA(pattern, &entry);
    const DWORD first_error = handle == INVALID_HANDLE_VALUE ? GetLastError() : ERROR_SUCCESS;
    free(pattern);
    if(handle == INVALID_HANDLE_VALUE) {
        /* An empty directory may have no wildcard matches. */
        if(first_error != ERROR_FILE_NOT_FOUND) report_windows(state, path, first_error);
        return;
    }
    for(;;) {
        if(!add_child(children, path, entry.cFileName)) {
            report(state, path, "cannot allocate directory entries");
            break;
        }
        if(!FindNextFileA(handle, &entry)) {
            const DWORD code = GetLastError();
            if(code != ERROR_NO_MORE_FILES) report_windows(state, path, code);
            break;
        }
    }
    if(!FindClose(handle)) report_windows(state, path, GetLastError());
#else
    const int flags = O_RDONLY | O_DIRECTORY | O_CLOEXEC | (explicit_input ? 0 : O_NOFOLLOW);
    const int descriptor = open(path, flags);
    if(descriptor < 0) {
        report_errno(state, path, errno);
        return;
    }
    DIR *directory = fdopendir(descriptor);
    if(directory == NULL) {
        const int code = errno;
        (void)close(descriptor);
        report_errno(state, path, code);
        return;
    }
    for(;;) {
        /* Other operations can set errno; reset it before every readdir call. */
        errno = 0;
        const struct dirent *entry = readdir(directory);
        if(entry == NULL) {
            if(errno != 0) report_errno(state, path, errno);
            break;
        }
        if(!add_child(children, path, entry->d_name)) {
            report(state, path, "cannot allocate directory entries");
            break;
        }
    }
    if(closedir(directory) != 0) report_errno(state, path, errno);
#endif
}

gm_walk_status gm_walk_path(
    const char *path, bool recursive, gm_file_visitor visit, gm_path_error_sink error, void *context
) {
    walk_state state = {.error = error, .context = context, .failed = false};
    if(path == NULL || path[0] == '\0' || visit == NULL) {
        report(&state, path != NULL ? path : "(null)", "invalid traversal arguments");
        return GM_WALK_ERROR;
    }

    path_list pending = {0};
    char *root = join_path("", path);
    if(root == NULL || !reserve_paths(&pending, 1)) {
        free(root);
        clear_paths(&pending);
        report(&state, path, "cannot allocate input path");
        return GM_WALK_ERROR;
    }
    pending.items[pending.count++] = root;

    bool explicit_input = true;
    bool stopped = false;
    while(pending.count > 0 && !stopped) {
        char *current = pending.items[--pending.count];
        const path_kind kind = classify_path(current, explicit_input, &state);
        if(kind == PATH_FILE) {
            stopped = !visit(current, explicit_input, context);
        } else if(kind == PATH_DIRECTORY) {
            if(!recursive) {
                report(&state, current, "directory requires --recursive (-r)");
            } else {
                path_list children = {0};
                read_directory(current, explicit_input, &children, &state);
                if(children.count > 1) {
                    qsort(children.items, children.count, sizeof(*children.items), compare_paths);
                }
                if(!reserve_paths(&pending, children.count)) {
                    report(&state, current, "cannot allocate pending paths");
                } else {
                    while(children.count > 0) {
                        pending.items[pending.count++] = children.items[--children.count];
                    }
                }
                clear_paths(&children);
            }
        }
        free(current);
        explicit_input = false;
    }
    clear_paths(&pending);
    if(stopped) return GM_WALK_STOPPED;
    return state.failed ? GM_WALK_ERROR : GM_WALK_OK;
}
