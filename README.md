# grepmorph

`grepmorph` is a small representation aware binary search tool.

The core idea: search for the **logical value** I actually care about across plausible binary/text
representations instead of trying to guess how that value was stored.

## Status

Raw/text morph searches and recursive directory discovery are implemented:

- a C17 build with CMake;
- a typed morph model and exact, case-sensitive byte matcher;
- explicit-file searches, including multiple inputs;
- opt-in `-r` / `--recursive` directory searches with deterministic ordering;
- literal queries and `--hex` binary queries (including NUL bytes);
- validated UTF-8 text with genuine UTF-16LE/BE conversion, including surrogate pairs;
- reusable query morphs searched together in one file pass;
- bounded, chunked file input with cross-boundary and overlapping matches;
- 64-bit absolute offsets and morph-labelled results;
- error reporting with nonzero exit status for incomplete searches;
- tests whose checks remain active in Debug and Release;
- optional ASan/UBSan builds and cross-platform GitHub Actions CI.

The operational morphs are **raw, utf8, utf16-le and utf16-be**. Numeric, hex-text,
Base64 and gap-search strategies are not implemented merely because their identifiers exist.
Directories require `-r`; explicitly named special files are rejected.

## Usage

```sh
./build/grepmorph 'DRAGONCAT' storage.bin accounts.bin
./build/grepmorph -r 'DRAGONCAT' data/ archives/
./build/grepmorph -r --text 'DRAGONCAT' data/ archives/
./build/grepmorph --morph utf16-le --morph utf16-be 'DRAGONCAT' storage.bin
./build/grepmorph -r --hex '00 ff 00' tests/fixtures/
./build/grepmorph --hex 'FD B1 04 00 00 01' capture.bin
./build/grepmorph -- '-query-starting-with-a-dash' example.bin
```

Queries are literal and case-sensitive by default. Backslashes are not escape sequences.
`--hex` accepts a nonempty, even number of ASCII hex digits, optionally separated by
ASCII whitespace. It specifies **raw query bytes**, not a hex-text morph or decoding of
the file. For example, `--hex '61 62 63'` searches for `abc`, not the text `616263`.

### Text and morph selection

Existing commands remain raw byte searches. Text conversion is explicit:

| Query mode | Behaviour |
| --- | --- |
| No mode option | Literal bytes, labelled `raw`; no UTF-8 validation. |
| `--hex` | Raw bytes supplied as hex, including NUL and non-UTF-8 bytes. |
| `--text` | Validate UTF-8 and search `utf8`, `utf16-le`, and `utf16-be`. |
| `--morph NAME` | Select `raw`, `utf8`, `utf16-le`, or `utf16-be`; repeat for a subset. |

`--hex`, `--text` and explicit `--morph` selection cannot be combined. Duplicate morphs,
unknown/unimplemented names and empty queries are errors. Selecting any text morph validates
the entire query as UTF-8 **before opening input paths**, even when raw is also selected.
An error names the zero-based byte offset at the start of the invalid sequence. Raw-only
searches continue accepting arbitrary bytes; malformed text is never silently replaced.

```sh
./build/grepmorph --text abc tests/fixtures/text-mixed.bin
```

```text
tests/fixtures/text-mixed.bin:0x0000000000000000:utf8
tests/fixtures/text-mixed.bin:0x0000000000000008:utf16-le
tests/fixtures/text-mixed.bin:0x0000000000000010:utf16-be
```

UTF-16 conversion is by Unicode scalar value, not by inserting zeros after UTF-8 bytes.
Supplementary scalars produce surrogate pairs. No BOM or string terminator is added or
required. A BOM explicitly present in the query is treated as a scalar and retained.
Matching is exact: no case folding, normalisation, grapheme equivalence or file-wide
encoding validation. Visually identical composed/decomposed strings remain distinct.

All byte offsets are considered, including odd UTF-16 positions. A morph label describes
**the pattern that matched**, not proof of the file's encoding: zero padding can allow
both endian forms of an ASCII query to match at shifted positions. Explicitly selecting
raw and utf8 emits two labelled records for their identical bytes, by design.

The command-line query bytes must be UTF-8 in text mode. POSIX users should use a UTF-8
terminal/locale. The Windows entry point still uses narrow CRT arguments and paths in the
system code page; arbitrary non-ASCII Windows shell input is **not yet guaranteed**. It may
be rejected or already altered before the CLI sees it. The portable C API accepts explicit
UTF-8 byte ranges (including embedded NUL); a Unicode-native Windows input/path layer is
separate work. No change to the C locale is used to guess the query's encoding.

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

Morphs are compiled once per query and reused for every file. All selected morphs share
one file read pass; hits within each file are ordered by byte offset, then by fixed morph
order (`raw`, `utf8`, `utf16-le`, `utf16-be`), independently of chunk sizes or option order.
The input buffer uses 64 KiB plus `longest morph length - 1` overlap bytes, not the whole file.
Unfinished tail starts wait for the next chunk; at EOF shorter morphs are still searched.
Query morph storage is separate and proportional to the query size, not file size.
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

The morph pipeline supports, or is planned to support, these search strategies:

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

Text tests exhaust all 1,112,064 Unicode scalar values, verify malformed/truncated UTF-8
rejection, and check ownership, BOM preservation and exact non-normalising behaviour.
Multi-pattern tests compare 1,600 generated cases against independent single-pattern
results, alongside EOF tails, differing pattern lengths, ties and high-offset arithmetic.
CLI tests exercise mode conflicts, selected morphs, recursive text searches and byte labels.
The external non-ASCII argv test is POSIX-only; explicit UTF-8 CLI-layer tests run everywhere.

### Platform and CI notes

On macOS the core is compiled with `_DARWIN_C_SOURCE=1` as well as `_POSIX_C_SOURCE=200809L`.
Darwin's headers hide `O_NOFOLLOW` under strict POSIX selection without this extension macro.
The no-follow flag remains active: there is no zero-valued fallback or removal of protection.
See Apple's [fcntl.h](https://github.com/apple-oss-distributions/xnu/blob/main/bsd/sys/fcntl.h).

CI checks out with a full-SHA pin to `actions/checkout` v7.0.1 (Node.js 24), read-only repository
permissions and no persisted credentials. Ubuntu jobs explicitly use `ubuntu-24.04` so the
announced `ubuntu-latest` image migration does not change this increment's Linux environment.
Windows/macOS remain on their existing `*-latest` labels. The workflow still requires real
cross-platform CI execution; a successful Linux run does not validate Darwin or Windows.

## Next implementation steps

1. Hex-text and numeric LE/BE morphs with explicit comparison/width semantics.
2. Base64-region decode-and-search rather than encoded-query substring matching.
3. Contextual and JSON result output.
4. Explicit `--max-gap N` ordered sparse matching.

A Unicode-native Windows argument/path layer remains a documented portability gap.

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
