# grepmorph

`grepmorph` is a small representation aware binary search tool.

The core idea: search for the **logical value** I actually care about across plausible binary/text
representations instead of trying to guess how that value was stored.

## Status

Initial scaffold only. The repository currently provides:

- a C17 build with CMake;
- a typed representation model;
- a safe exact in memory matcher as the first core primitive;
- a CLI shell (`--help`, `--version`);
- CTest coverage for the matcher and CLI;
- optional ASan/UBSan builds on GCC/Clang;
- GitHub Actions CI on Linux, macOS and Windows.

No filesystem scanning or representation expansion is wired into the CLI yet.

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

Strict local build:

```sh
cmake -S . -B build \
  -DGREPMORPH_WARNINGS_AS_ERRORS=ON \
  -DGREPMORPH_ENABLE_SANITIZERS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

## Initial plan

1. Recursive file discovery and chunked/mapped file input.
2. Raw exact search exposed through the CLI.
3. Query parsing including C-style escaped bytes.
4. UTF-8 / UTF-16LE / UTF-16BE pattern generation.
5. Hex-text and numeric LE/BE representations.
6. Base64-region decode-and-search rather than encoded-query substring matching.
7. Contextual and JSON result output.
8. Explicit `--max-gap N` ordered sparse matching.

Performance work will follow correctness and representation semantics stability.

## Provenance

The concept is inspired by [Luigi Auriemma's](https://aluigi.altervista.org/) `mygrep 0.1`, which searches strings in several
representations including binary patterns, UTF-16-like data, Base64 and hexadecimal forms.

This repository starts as a fresh implementation. No source from `mygrep 0.1` is included in the
initial scaffold.

## Licence

A public licence will be selected before public release.
