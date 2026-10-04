# grepmorph

`grepmorph` is a small representation aware binary search tool.

The core idea: search for the **logical value** I actually care about across plausible binary/text
representations instead of trying to guess how that value was stored.

## Status

The first usable search increment is implemented:

- a C17 build with CMake;
- a typed morph model and exact, case-sensitive byte matcher;
- explicit-file searches, including multiple inputs;
- literal queries and `--hex` binary queries (including NUL bytes);
- bounded, chunked file input with cross-boundary and overlapping matches;
- 64-bit absolute offsets and morph-labelled results;
- error reporting with nonzero exit status for incomplete searches;
- tests whose checks remain active in Debug and Release;
- optional ASan/UBSan builds and cross-platform GitHub Actions CI.

Only the **raw morph** is operational. UTF-16, numeric, hex-text, Base64 and gap-search
strategies are not implemented merely because their identifiers exist. Recursive directory
walking is the next input feature; directories and special files are currently rejected.

## Usage

```sh
./build/grepmorph 'DRAGONCAT' storage.bin accounts.bin
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
Other readable files are still searched after an input error.

Exit codes are `0` for at least one match, `1` for no matches, and `2` for invalid input,
file errors or output errors. An error takes precedence over any matches already emitted.
`--help` and `--version` return `0` on success. Process termination by operating-system
signals retains its normal behaviour (for example, a broken Unix pipe can raise `SIGPIPE`).

The input buffer uses 64 KiB plus `query length - 1` overlap bytes, not the whole file.
There is no file-size-sized allocation or conversion of file sizes to `int`. The scanner
reads from the current stream position; the stream API's explicit base offset supports
embedding and 64-bit offset tests without seeking or allocating multi-gigabyte fixtures.

Explicit symlinks to regular files follow platform path resolution; there is no recursive
symlink traversal. Windows currently uses narrow C-runtime paths (the system code page),
not a full Unicode-native path layer. Scanning a changing file does not provide a snapshot.

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

## Next implementation steps

1. Recursive file discovery with explicit symlink and error policies.
2. UTF-8 / UTF-16LE / UTF-16BE pattern generation and per-morph comparison semantics.
3. Hex-text and numeric LE/BE morphs.
4. Base64-region decode-and-search rather than encoded-query substring matching.
5. Contextual and JSON result output.
6. Explicit `--max-gap N` ordered sparse matching.

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
