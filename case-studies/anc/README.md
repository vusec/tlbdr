# TLB;DR case study: AnC

A patch for the native AnC [[1](https://vusec.net/projects/anc)] attack code incorporating optimal eviction sets.
This work is part of [TLB;DR](https://vusec.net/projects/tlbdr).

## Contents
### Patches
- `revanc-ninja-evsets.patch` — the patch adding optimal eviction set functionality switched via config var.
- `revanc-ninja-enable.patch` — additional patch setting default config vars to use optimal eviction sets.
### Scripts
- `measure.sh` — fetch AnC code, apply patches, build and measure both original and optimized AnC
- `plot.py` — produce PDF (or PGF) graphs comparing the measurements made by `measure.sh`
### Utils
- `plotanc.py` — low-level implementation of plotting measurements

## How to
### Reproduce results
1. Ensure you are running on an Intel Kaby Lake (model number 7XXX) CPU, and have git, a C compiler, Python 3, numpy, and matplotlib installed.
1. Run `measure.sh` which will produce `anc-naive-10.txt` and `anc-ninja-10.txt`.
	- (optional) examine output files by hand
1. Run `plot.py` to produce a comparative plot similar to the one in the paper.

### Customize
- `measure.sh` takes optional arguments `NMEAS`—the number of executions of the AnC binary and `NRUNS`—the number of AnC attacks performed by each execution.
- `NMEAS` can also be set as a tweakable in `plotanc.py`.
