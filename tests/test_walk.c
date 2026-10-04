#include "walk.h"
#include "file_search.h"
#include "test_check.h"

#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#else
#include <sys/resource.h>
#include <unistd.h>
#endif

typedef struct {
    size_t visited;
    size_t errors;
    size_t explicit_count;
    size_t stop_after;
    char *names[256];
    const char *remove_on_visit;
} visits;

static char *join(const char *parent, const char *name) {
    const size_t length = strlen(parent) + strlen(name) + 2;
    char *path = malloc(length);
    CHECK(path != NULL);
    CHECK(snprintf(path, length, "%s/%s", parent, name) > 0);
    return path;
}

static void directory(const char *path) {
#ifdef _WIN32
    CHECK(_mkdir(path) == 0);
#else
    CHECK(mkdir(path, 0700) == 0);
#endif
}

static void file(const char *path) {
    FILE *stream = NULL;
#ifdef _MSC_VER
    CHECK(fopen_s(&stream, path, "wb") == 0);
#else
    stream = fopen(path, "wb");
#endif
    CHECK(stream != NULL);
    CHECK(fwrite("abc", 1, 3, stream) == 3);
    CHECK(fclose(stream) == 0);
}

static void error(const char *path, const char *message, void *context) {
    visits *result = context;
    CHECK(path != NULL && message != NULL && message[0] != '\0');
    ++result->errors;
}

static bool visit(const char *path, bool explicit_input, void *context) {
    visits *result = context;
    CHECK(result->visited < 256);
    const size_t length = strlen(path) + 1;
    result->names[result->visited] = malloc(length);
    CHECK(result->names[result->visited] != NULL);
    memcpy(result->names[result->visited], path, length);
    ++result->visited;
    if(explicit_input) ++result->explicit_count;
    FILE *stream = gm_open_regular_file_with_links(path, explicit_input);
    CHECK(stream != NULL);
    CHECK(fgetc(stream) == 'a');
    CHECK(fclose(stream) == 0);
    if(result->remove_on_visit != NULL) {
        CHECK(remove(result->remove_on_visit) == 0);
        result->remove_on_visit = NULL;
    }
    return result->stop_after == 0 || result->visited < result->stop_after;
}

static void clear(visits *result) {
    for(size_t i = 0; i < result->visited; ++i) free(result->names[i]);
    *result = (visits){0};
}

static void basic(const char *root) {
    visits result = {0};
    CHECK(gm_walk_path(root, true, visit, error, &result) == GM_WALK_OK);
    CHECK(result.visited == 0 && result.errors == 0);
    char *path = join(root, "file.bin");
    file(path);
    CHECK(gm_walk_path(path, false, visit, error, &result) == GM_WALK_OK);
    CHECK(result.visited == 1 && result.explicit_count == 1);
    clear(&result);
    CHECK(gm_walk_path(root, false, visit, error, &result) == GM_WALK_ERROR);
    CHECK(result.visited == 0 && result.errors == 1);
    clear(&result);
    CHECK(gm_walk_path(root, true, visit, error, &result) == GM_WALK_OK);
    CHECK(result.visited == 1 && result.explicit_count == 0);
    clear(&result);
    free(path);
}

static void invalid(const char *root) {
    visits result = {0};
    CHECK(gm_walk_path(NULL, true, visit, error, &result) == GM_WALK_ERROR);
    CHECK(gm_walk_path("", true, visit, error, &result) == GM_WALK_ERROR);
    CHECK(gm_walk_path(root, true, NULL, error, &result) == GM_WALK_ERROR);
    char *path = join(root, "missing");
    CHECK(gm_walk_path(path, true, visit, error, &result) == GM_WALK_ERROR);
    CHECK(result.visited == 0 && result.errors == 4);
    CHECK(gm_walk_path(path, true, visit, NULL, NULL) == GM_WALK_ERROR);
    free(path);
    clear(&result);
}

static void order(const char *root) {
    char *sub = join(root, "b-sub");
    directory(sub);
    char *paths[] = {join(root, "a.bin"), join(sub, "a.bin"),
                     join(sub, "z.bin"), join(root, "z.bin")};
    for(size_t i = 4; i > 0; --i) file(paths[i - 1]);
    visits result = {0};
    CHECK(gm_walk_path(root, true, visit, error, &result) == GM_WALK_OK);
    CHECK(result.visited == 4 && result.errors == 0 && result.explicit_count == 0);
    for(size_t i = 0; i < 4; ++i) {
        CHECK(strcmp(paths[i], result.names[i]) == 0);
        free(paths[i]);
    }
    free(sub);
    clear(&result);
}

static void wide_or_stop(const char *root, bool stop) {
    for(size_t i = 0; i < 200; ++i) {
        char name[32];
        CHECK(snprintf(name, sizeof(name), "%03zu.bin", i) > 0);
        char *path = join(root, name);
        file(path);
        free(path);
    }
    for(size_t attempt = 0; attempt < (stop ? 50u : 1u); ++attempt) {
        visits result = {.stop_after = stop ? 1 : 0};
        CHECK(gm_walk_path(root, true, visit, error, &result) ==
              (stop ? GM_WALK_STOPPED : GM_WALK_OK));
        CHECK(result.visited == (stop ? 1u : 200u) && result.errors == 0);
        clear(&result);
    }
}

static void vanished(const char *root) {
    char *a = join(root, "a.bin");
    char *b = join(root, "b.bin");
    char *c = join(root, "c.bin");
    file(a); file(b); file(c);
    visits result = {.remove_on_visit = b};
    CHECK(gm_walk_path(root, true, visit, error, &result) == GM_WALK_ERROR);
    CHECK(result.visited == 2 && result.errors == 1);
    CHECK(strcmp(result.names[0], a) == 0 && strcmp(result.names[1], c) == 0);
    clear(&result);
    free(a); free(b); free(c);
}

static void opener(const char *root) {
    CHECK(gm_open_regular_file_with_links(NULL, false) == NULL);
    CHECK(gm_open_regular_file_with_links("", false) == NULL);
    CHECK(gm_open_regular_file_with_links(root, false) == NULL);
    char *path = join(root, "file.bin");
    CHECK(gm_open_regular_file_with_links(path, false) == NULL);
    file(path);
    FILE *stream = gm_open_regular_file_with_links(path, false);
    CHECK(stream != NULL && fgetc(stream) == 'a');
    CHECK(fclose(stream) == 0);
    free(path);
}

#ifndef _WIN32
static void links(const char *root) {
    char *sub = join(root, "sub");
    directory(sub);
    char *real = join(sub, "real.bin");
    file(real);
    char *loop = join(sub, "loop");
    char *dangling = join(root, "dangling");
    char *file_link = join(root, "file-link");
    char *dir_link = join(root, "dir-link");
    CHECK(symlink("..", loop) == 0);
    CHECK(symlink("missing-target", dangling) == 0);
    CHECK(symlink("sub/real.bin", file_link) == 0);
    CHECK(symlink("sub", dir_link) == 0);
    visits result = {0};
    CHECK(gm_walk_path(root, true, visit, error, &result) == GM_WALK_OK);
    CHECK(result.visited == 1 && result.errors == 0);
    clear(&result);
    CHECK(gm_walk_path(file_link, false, visit, error, &result) == GM_WALK_OK);
    CHECK(result.visited == 1 && result.explicit_count == 1);
    clear(&result);
    CHECK(gm_walk_path(dir_link, true, visit, error, &result) == GM_WALK_OK);
    CHECK(result.visited == 1 && result.explicit_count == 0 && result.errors == 0);
    clear(&result);
    CHECK(gm_walk_path(dangling, true, visit, error, &result) == GM_WALK_ERROR);
    CHECK(result.errors == 1);
    CHECK(gm_open_regular_file_with_links(file_link, false) == NULL);
    clear(&result);
    free(sub); free(real); free(loop); free(dangling); free(file_link); free(dir_link);
}

static void special(const char *root) {
    char *pipe = join(root, "fifo");
    char *real = join(root, "real.bin");
    CHECK(mkfifo(pipe, 0600) == 0);
    file(real);
    visits result = {0};
    CHECK(gm_walk_path(root, true, visit, error, &result) == GM_WALK_OK);
    CHECK(result.visited == 1 && result.errors == 0);
    clear(&result);
    CHECK(gm_walk_path(pipe, true, visit, error, &result) == GM_WALK_ERROR);
    CHECK(result.errors == 1 && result.visited == 0);
    CHECK(gm_open_regular_file_with_links(pipe, false) == NULL);
    clear(&result);
    free(pipe); free(real);
}

static int permissions(const char *root) {
    if(geteuid() == 0) {
        puts("SKIP: permission checks require an unprivileged user");
        return 77;
    }
    char *sub = join(root, "a-denied");
    directory(sub);
    char *real = join(root, "z-readable.bin");
    file(real);
    CHECK(chmod(sub, 0) == 0);
    visits result = {0};
    const gm_walk_status status = gm_walk_path(root, true, visit, error, &result);
    CHECK(chmod(sub, 0700) == 0); /* Restore even before asserting the result. */
    CHECK(status == GM_WALK_ERROR);
    CHECK(result.visited == 1 && result.errors == 1);
    clear(&result);
    free(sub); free(real);
    return 0;
}

static void deep_low_descriptors(const char *root) {
#ifdef __linux__
    const char *component = "long-component"; /* Also exceed 1 KiB on Linux. */
#else
    const char *component = "d"; /* Stay within macOS's shorter OS path limit. */
#endif
    char *path = join(root, component);
    for(size_t i = 0; i < 100; ++i) {
        directory(path);
        char *next = join(path, component);
        free(path);
        path = next;
    }
    file(path);
#ifdef __linux__
    CHECK(strlen(path) > 1024);
#endif
    struct rlimit original;
    CHECK(getrlimit(RLIMIT_NOFILE, &original) == 0);
    struct rlimit limited = original;
    if(limited.rlim_cur > 32) limited.rlim_cur = 32;
    CHECK(setrlimit(RLIMIT_NOFILE, &limited) == 0);
    visits result = {0};
    const gm_walk_status status = gm_walk_path(root, true, visit, error, &result);
    CHECK(setrlimit(RLIMIT_NOFILE, &original) == 0);
    CHECK(status == GM_WALK_OK && result.visited == 1 && result.errors == 0);
    clear(&result);
    free(path);
}
#endif

int main(int argc, char **argv) {
    CHECK(argc == 3);
    const char *name = argv[1];
    const char *root = argv[2];
    if(strcmp(name, "basic") == 0) basic(root);
    else if(strcmp(name, "invalid") == 0) invalid(root);
    else if(strcmp(name, "order") == 0) order(root);
    else if(strcmp(name, "wide") == 0) wide_or_stop(root, false);
    else if(strcmp(name, "stop") == 0) wide_or_stop(root, true);
    else if(strcmp(name, "vanished") == 0) vanished(root);
    else if(strcmp(name, "opener") == 0) opener(root);
#ifndef _WIN32
    else if(strcmp(name, "links") == 0) links(root);
    else if(strcmp(name, "special") == 0) special(root);
    else if(strcmp(name, "permissions") == 0) return permissions(root);
    else if(strcmp(name, "deep_low_descriptors") == 0) deep_low_descriptors(root);
#endif
    else CHECK(false);
    return 0;
}
