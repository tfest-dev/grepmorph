#include "cli.h"
#include "test_check.h"

#include <string.h>

/* Supply explicit UTF-8 bytes to the CLI layer, independently of the host's
 * shell/CRT argument encoding. External Unicode argv is also tested on POSIX. */
int main(int argc, char **argv) {
    CHECK(argc == 3);
    const char *name = argv[1];
    const unsigned char invalid_bytes[] = {'a', 0xed, 0xa0, 0x80, 0};
    const unsigned char valid_bytes[] = {'a', 0xc3, 0xa9, 0xf0, 0x9f, 0x98, 0x80, 0};
    const unsigned char raw_bytes[] = {0xff, 0};
    char invalid[sizeof(invalid_bytes)], valid[sizeof(valid_bytes)], raw[sizeof(raw_bytes)];
    /* memcpy preserves object bytes without an out-of-range signed-char cast. */
    memcpy(invalid, invalid_bytes, sizeof(invalid));
    memcpy(valid, valid_bytes, sizeof(valid));
    memcpy(raw, raw_bytes, sizeof(raw));
    CHECK((unsigned char)invalid[1] == 0xed && strlen(invalid) == 4);
    CHECK((unsigned char)valid[6] == 0x80 && strlen(valid) == 7);
    CHECK((unsigned char)raw[0] == 0xff && strlen(raw) == 1);
    if(strcmp(name, "invalid_text") == 0) {
        char *args[] = {"grepmorph", "--text", invalid, argv[2]};
        CHECK(gm_cli_run(4, args) == 2);
    } else if(strcmp(name, "invalid_selected") == 0) {
        char *args[] = {"grepmorph", "--morph", "raw", "--morph", "utf16-be", invalid, argv[2]};
        CHECK(gm_cli_run(7, args) == 2);
    } else if(strcmp(name, "unicode") == 0) {
        char *args[] = {"grepmorph", "--text", valid, argv[2]};
        CHECK(gm_cli_run(4, args) == 0);
    } else if(strcmp(name, "raw_invalid_utf8") == 0) {
        char *args[] = {"grepmorph", raw, argv[2]};
        CHECK(gm_cli_run(3, args) == 0);
    } else CHECK(0);
    return 0;
}
