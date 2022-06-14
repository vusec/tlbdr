# Cache Ninja

A Rust application to model CPU cache and TLB behavior using graph algorithms.

Used to generate superoptimal TLB eviction sets in [TLB;DR](https://vusec.net/tlbdr).

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

* `isnprio` -- whether to prioritize instruction fetches over data loads when all else is equal (_default_: **no**)
