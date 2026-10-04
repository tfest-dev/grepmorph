#include "file_search.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

_Static_assert(SIZE_MAX <= UINT64_MAX, "size_t must fit in uint64_t");

typedef struct {
    uint64_t base_offset;
    gm_match_sink sink;
    void *context;
} stream_sink;

static void emit_absolute_match(const gm_match *match, void *context) {
    const stream_sink *state = context;
    gm_match absolute = *match;
    /* The entire window has already been checked against UINT64_MAX. */
    absolute.offset += state->base_offset;
    state->sink(&absolute, state->context);
}

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
) {
    if(match_count != NULL) {
        *match_count = 0;
    }
    if(stream == NULL || needle == NULL || needle_length == 0 || chunk_size == 0 ||
       match_count == NULL || needle_length - 1 > SIZE_MAX - chunk_size) {
        return GM_SCAN_INVALID_ARGUMENT;
    }

    const size_t capacity = chunk_size + needle_length - 1;
    uint8_t *buffer = malloc(capacity);
    if(buffer == NULL) {
        return GM_SCAN_NO_MEMORY;
    }

    size_t carry = 0;
    uint64_t consumed = 0;
    gm_scan_status status = GM_SCAN_OK;
    for(;;) {
        const size_t received = fread(buffer + carry, 1, chunk_size, stream);
        if(received == 0) {
            if(ferror(stream)) {
                status = GM_SCAN_IO_ERROR;
            }
            break;
        }

        /* Check before addition, including the last byte's absolute offset. */
        if((uint64_t)received > UINT64_MAX - consumed ||
           base_offset > UINT64_MAX - (consumed + (uint64_t)received - 1)) {
            status = GM_SCAN_OFFSET_OVERFLOW;
            break;
        }

        const size_t available = carry + received;
        stream_sink state = {
            .base_offset = base_offset + consumed - (uint64_t)carry,
            .sink = sink,
            .context = context,
        };
        const size_t found = gm_search_exact(
            buffer, available, needle, needle_length, morph,
            sink != NULL ? emit_absolute_match : NULL, &state
        );
        /* For a nonempty pattern, there cannot be more matches than bytes. */
        *match_count += (uint64_t)found;
        consumed += (uint64_t)received;

        if(ferror(stream)) {
            status = GM_SCAN_IO_ERROR;
            break;
        }

        /* Fewer than needle_length bytes means no full match is rescanned. */
        carry = available < needle_length - 1 ? available : needle_length - 1;
        if(carry != 0) {
            memmove(buffer, buffer + available - carry, carry);
        }
    }

    free(buffer);
    return status;
}

const char *gm_scan_status_name(gm_scan_status status) {
    switch(status) {
        case GM_SCAN_OK:               return "ok";
        case GM_SCAN_INVALID_ARGUMENT: return "invalid search arguments or size overflow";
        case GM_SCAN_NO_MEMORY:        return "cannot allocate search buffer";
        case GM_SCAN_IO_ERROR:         return "file read failed";
        case GM_SCAN_OFFSET_OVERFLOW:  return "file offset exceeds uint64_t range";
        default:                      return "unknown search error";
    }
}

FILE *gm_open_regular_file(const char *path) {
    return gm_open_regular_file_with_links(path, true);
}

FILE *gm_open_regular_file_with_links(const char *path, bool follow_links) {
    if(path == NULL || path[0] == '\0') {
        errno = EINVAL;
        return NULL;
    }
#ifdef _WIN32
    if(!follow_links) {
        const HANDLE handle = CreateFileA(
            path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            NULL, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, NULL
        );
        if(handle == INVALID_HANDLE_VALUE) {
            const DWORD code = GetLastError();
            switch(code) {
                case ERROR_FILE_NOT_FOUND:
                case ERROR_PATH_NOT_FOUND: errno = ENOENT; break;
                case ERROR_ACCESS_DENIED:
                case ERROR_SHARING_VIOLATION: errno = EACCES; break;
                case ERROR_TOO_MANY_OPEN_FILES: errno = EMFILE; break;
                default: errno = EIO; break;
            }
            return NULL;
        }
        BY_HANDLE_FILE_INFORMATION information;
        if(GetFileType(handle) != FILE_TYPE_DISK ||
           !GetFileInformationByHandle(handle, &information) ||
           (information.dwFileAttributes &
            (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) != 0) {
            (void)CloseHandle(handle);
            errno = EINVAL;
            return NULL;
        }
        const int descriptor = _open_osfhandle((intptr_t)handle, _O_RDONLY | _O_BINARY);
        if(descriptor < 0) {
            const int code = errno;
            (void)CloseHandle(handle);
            errno = code;
            return NULL;
        }
        FILE *result = _fdopen(descriptor, "rb");
        if(result == NULL) {
            const int code = errno;
            (void)_close(descriptor);
            errno = code;
        }
        return result;
    }
    struct _stat64 metadata;
    if(_stat64(path, &metadata) != 0) {
        return NULL;
    }
    if((metadata.st_mode & _S_IFMT) != _S_IFREG) {
        errno = EINVAL;
        return NULL;
    }
    FILE *stream = NULL;
#ifdef _MSC_VER
    if(fopen_s(&stream, path, "rb") != 0) {
        return NULL;
    }
#else
    stream = fopen(path, "rb");
    if(stream == NULL) {
        return NULL;
    }
#endif
    if(_fstat64(_fileno(stream), &metadata) != 0 ||
       (metadata.st_mode & _S_IFMT) != _S_IFREG) {
        (void)fclose(stream);
        errno = EINVAL;
        return NULL;
    }
    return stream;
#else
    /* Nonblocking open prevents a named pipe from hanging before fstat. */
    const int descriptor = open(
        path, O_RDONLY | O_NONBLOCK | O_CLOEXEC | (follow_links ? 0 : O_NOFOLLOW)
    );
    if(descriptor < 0) {
        return NULL;
    }
    struct stat metadata;
    if(fstat(descriptor, &metadata) != 0) {
        const int saved_errno = errno;
        (void)close(descriptor);
        errno = saved_errno;
        return NULL;
    }
    if(!S_ISREG(metadata.st_mode)) {
        (void)close(descriptor);
        errno = EINVAL;
        return NULL;
    }
    FILE *stream = fdopen(descriptor, "rb");
    if(stream == NULL) {
        const int saved_errno = errno;
        (void)close(descriptor);
        errno = saved_errno;
    }
    return stream;
#endif
}
