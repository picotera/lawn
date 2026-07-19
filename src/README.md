# src

Source for the Lawn timer data structure and its supporting code.

## Timer implementations

- **`lawn.c` / `lawn.h`** - the reference Lawn (Queue-Map algorithm, see
  [`../docs/Algorithm.md`](../docs/Algorithm.md)). Owning, key-addressed API:
  the store allocates nodes and copies your key, and you cancel/look up a timer
  by an arbitrary opaque key.
- **`lawn2.c` / `lawn2.h`** - an optimized, allocation-free Lawn. Same Queue-Map
  algorithm (a differential test verifies identical expiry schedules), but with
  intrusive handle-based nodes (no per-insert malloc, no key copy), O(1) delete
  by node pointer, and an open-addressing TTL->queue table.
- **`lawn.py`** - a pure-Python Lawn reference.

Which to use, and how each compares to a timing wheel, is in
[`../docs/implementations.md`](../docs/implementations.md).

## Supporting code

- **`utils/`** - portable non-glibc hashmap (`hashmap.{c,h}`), logical/wall-clock
  time (`millisecond_time.{c,h}`), hash functions, and a hierarchical timer wheel
  (`timerwheel.{c,h}`) used for comparison.
- **`trie/`**, **`sparsehash/`** - alternative data structures explored alongside
  Lawn.

## Tests and benchmarks

- **`tests/`** - C unit tests for the library (`make -C tests`).
- **`benchmarks/`** - the Python and C benchmark suites comparing Lawn, lawn2,
  and timing wheels; see [`benchmarks/README.md`](benchmarks/README.md).

## Building

`make` builds the C library and tests. The C sources are portable C11 (Linux,
macOS, the BSDs).
