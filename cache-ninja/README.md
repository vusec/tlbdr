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
