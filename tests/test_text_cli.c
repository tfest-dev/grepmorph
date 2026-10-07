#include "cli.h"
#include "test_check.h"

#include <string.h>

/* Supply explicit UTF-8 bytes to the CLI layer, independently of the host's
 * shell/CRT argument encoding. External Unicode argv is also tested on POSIX. */
int main(int argc, char **argv) {
    CHECK(argc == 3);
    const char *name = argv[1];
    char invalid[] = {'a', (char)0xed, (char)0xa0, (char)0x80, 0};
    char valid[] = {'a', (char)0xc3, (char)0xa9, (char)0xf0,
                    (char)0x9f, (char)0x98, (char)0x80, 0};
    char raw[] = {(char)0xff, 0};
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
