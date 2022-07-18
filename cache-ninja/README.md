# Cache Ninja

A Rust application to model CPU cache and TLB behavior using graph algorithms.
Used to generate optimized TLB eviction sets as part of [TLB;DR](https://vusec.net/tlbdr).

## Getting Started

### Prerequisites
* Rust + Cargo >= 1.49 (might work with older versions, not tested)

### Building & Running
Build & run like a normal cargo project:
```
cargo build
cargo run
```
For ideal performance make sure to build/run the optimized `release` target:
```
cargo build --release
cargo run --release
```

## Customizing

To change the used cache preset or pathfinding algorithm (un)comment the relevant lines in `src/main.rs` and rebuild/re-run.

### Selecting a Cache Preset

The `preset` module includes reverse-engineered profiles for the TLBs and data caches of common Intel x86 processors.

### Compile-time Features
(enable with `cargo [build|run] --features FEATURE[,FEATURE,...]`)

* `nol1hit` — do not consider L1 hits when exploring (only applies to `H2SRP` policies; _default_: **yes**)
* `isnprio` — prioritize instruction fetches over data loads when all else is equal (_default_: **no**)
* `nohit` — do not consider cache hits when exploring (_default_: **no**)

### Interpreting the Output
Running `cache-ninja` will produce output describing the state of the TLB through the course of accessing an eviction set using representations of its internal data structures.
Below is an explanation of these data structures and their meaning.

* `PVec[...]` — the state of a TLB component represented by a **permutation vector** with entries in a particular order
* `T`, `X` — entries representing the eviction target and "don't care" entries, respectively
* `P(N, true/false)` — entry accessible to the attacker; `N` is the entry ID, to distinguish it from others, and `true/false` represents whether the entry has been loaded via an instruction fetch or not
* `Origin { isnfetch: true/false }` — the origin of a memory access; currently represents whether the access occurs through an instruction fetch or a data load
* `Miss(ENTRY, ORIGIN)` — a TLB miss when accessing `ENTRY` through `ORIGIN`
* `Hit(N, ENTRY, ORIGIN)` — a hit on a TLB component on permutation vector position `N` by accessing `ENTRY` through `ORIGIN`
