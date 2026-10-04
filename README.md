# grepmorph

`grepmorph` is a small representation aware binary search tool.

The core idea: search for the **logical value** I actually care about across plausible binary/text
representations instead of trying to guess how that value was stored.

## Status

Raw file search and recursive directory discovery are implemented:

- a C17 build with CMake;
- a typed morph model and exact, case-sensitive byte matcher;
- explicit-file searches, including multiple inputs;
- opt-in `-r` / `--recursive` directory searches with deterministic ordering;
- literal queries and `--hex` binary queries (including NUL bytes);
- bounded, chunked file input with cross-boundary and overlapping matches;
- 64-bit absolute offsets and morph-labelled results;
- error reporting with nonzero exit status for incomplete searches;
- tests whose checks remain active in Debug and Release;
- optional ASan/UBSan builds and cross-platform GitHub Actions CI.

Only the **raw morph** is operational. UTF-16, numeric, hex-text, Base64 and gap-search
strategies are not implemented merely because their identifiers exist. Directories require
`-r`; explicitly named special files are rejected.

## Usage

```sh
./build/grepmorph 'DRAGONCAT' storage.bin accounts.bin
./build/grepmorph -r 'DRAGONCAT' data/ archives/
./build/grepmorph -r --hex '00 ff 00' tests/fixtures/
./build/grepmorph --hex 'FD B1 04 00 00 01' capture.bin
./build/grepmorph -- '-query-starting-with-a-dash' example.bin
```

Queries are literal and case-sensitive by default. Backslashes are not escape sequences.
`--hex` accepts a nonempty, even number of ASCII hex digits, optionally separated by
ASCII whitespace. It specifies **raw query bytes**, not a hex-text morph or decoding of
the file. For example, `--hex '61 62 63'` searches for `abc`, not the text `616263`.

Every hit is a byte offset from the start of the file, including overlapping hits:

```text
example.bin:0x0000000000000012:raw
```

Backslashes and ASCII control characters in filenames are escaped in output. Results go
to standard output; diagnostics go to standard error. Files are scanned in argument order.
Within each directory, entries are visited depth-first in bytewise name order (not locale
collation). Other readable entries and subsequent input paths are still searched after an
input error. Overlapping input paths are searched independently, so naming a file and its
parent directory can report that file twice.

Exit codes are `0` for at least one match, `1` for no matches, and `2` for invalid input,
file errors or output errors. An error takes precedence over any matches already emitted.
`--help` and `--version` return `0` on success. Process termination by operating-system
signals retains its normal behaviour (for example, a broken Unix pipe can raise `SIGPIPE`).

The input buffer uses 64 KiB plus `query length - 1` overlap bytes, not the whole file.
There is no file-size-sized allocation or conversion of file sizes to `int`. The scanner
reads from the current stream position; the stream API's explicit base offset supports
embedding and 64-bit offset tests without seeking or allocating multi-gigabyte fixtures.

### Directory traversal policy

Use `-r` or `--recursive` before the query to enable directory searching. Without it,
directories produce a diagnostic and exit status `2`; explicit file searches are unchanged.

- **Explicit paths:** follow platform link resolution. A symlink to a regular file is
  searchable, and an explicitly named directory link is searchable with `-r`.
- **Discovered entries:** skip symbolic links, all Windows reparse points (including
  junctions), and special files such as FIFOs, devices and sockets. Broken discovered
  links are skipped; an explicitly named broken link is an error.
- **Coverage and errors:** include hidden files and directories. No `.gitignore`, `.git`
  or build-directory exclusions are applied. Missing/unreadable entries and directory
  enumeration failures produce diagnostics and exit status `2`, even when other files match.

No `--follow` option, filtering, deduplication or parallel traversal is implemented. Skipped
entry types are part of the defined search scope, not input errors. Searching `-r .` therefore
includes `.git/` and `build/`; target a data directory for a narrower scan.

The walker uses an explicit heap-backed pending-path stack rather than recursive C calls.
It closes directory enumeration handles before descending. Path storage grows with queued
entries (and sorting requires collecting each directory's entries); it is not constant-memory
metadata traversal, but it does not collect the entire tree before searching. Paths are allocated
dynamically rather than truncated into fixed-size arrays; operating-system path limits still apply.

Discovered regular files are opened without following a final symlink/reparse point. Ancestor
path components still use platform path resolution. Directory traversal is path-based, not a
snapshot or a security boundary against concurrent, hostile namespace changes. Windows still
uses narrow paths in the system code page, not a full Unicode-native/long-path layer.

## Terminology

The code uses **morph** for a concrete search form derived from a logical query. For example, a
query may produce raw, UTF-16LE, hexadecimal-text or integer-endian morphs. A **match** records the
file offset and the morph that produced it. Representation remains the general user facing concept.

## Shape

A query will eventually produce one or more typed search strategies, for example:

- raw bytes / UTF-8;
- UTF-16LE and UTF-16BE;
- hexadecimal text;
- decoded Base64 regions;
- integer values in little- and big-endian forms;
- bounded ordered-gap matching for intentionally sparse data.

Matches should retain the representation that produced them and expose stable offsets plus useful
context. Structured JSON output can layer on later.

## Build

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

Strict Debug/sanitiser build (GCC or Clang):

```sh
cmake -S . -B build-sanitised \
  -DCMAKE_BUILD_TYPE=Debug \
  -DGREPMORPH_WARNINGS_AS_ERRORS=ON \
  -DGREPMORPH_ENABLE_SANITIZERS=ON
cmake --build build-sanitised --config Debug
ctest --test-dir build-sanitised -C Debug --output-on-failure
```

Also test Release: the previous scaffold used `assert()` for checks, which disappears
when `NDEBUG` is defined. The tests now use always-on checks instead.

```sh
cmake -S . -B build-release \
  -DCMAKE_BUILD_TYPE=Release \
  -DGREPMORPH_WARNINGS_AS_ERRORS=ON
cmake --build build-release --config Release
ctest --test-dir build-release -C Release --output-on-failure
```

The suite covers tiny non-terminated allocations, invalid lengths, NUL/high-bit bytes,
overlaps, every boundary position around small chunks, patterns larger than a chunk,
64-bit offset arithmetic, overflow rejection, CLI validation and error precedence.
A deterministic differential test compares streaming against in-memory results in 1,120
cases. POSIX additionally tests a forced file-read error. These tests do not certify every
possible input, and high-offset tests do not scan a physical multi-gigabyte fixture.

Traversal tests cover ordered nested results, multiple roots, hidden files, trailing separators,
`.` / `..` roots, link loops, broken links, vanished queued files, early-stop cleanup, wide
directories, and raw/hex searches across chunk boundaries within subdirectories. POSIX tests
also cover FIFOs, unreadable directories, and a 100-level tree with a 32-descriptor soft limit.
Permission tests are explicitly skipped when run as root. Cross-platform symlink fixtures are
explicitly skipped when the host cannot create symbolic links (for example, restricted Windows
accounts); those skips are not evidence that the link behaviour passed.

## Next implementation steps

1. UTF-8 / UTF-16LE / UTF-16BE pattern generation and per-morph comparison semantics.
2. Hex-text and numeric LE/BE morphs.
3. Base64-region decode-and-search rather than encoded-query substring matching.
4. Contextual and JSON result output.
5. Explicit `--max-gap N` ordered sparse matching.

C-style escaped-query syntax can be added explicitly later; `--hex` already provides
an unambiguous route for arbitrary binary queries.
Performance work will follow correctness and representation semantics stability.

## Provenance

The concept is inspired by [Luigi Auriemma's](https://aluigi.altervista.org/) `mygrep 0.1`, which 
searches strings in several representations including binary patterns, UTF-16-like data, Base64 and
hexadecimal forms.

This repository starts as a fresh implementation. No source from `mygrep 0.1` is included in the
initial scaffold.

## Licence

A public licence will be selected before public release.
