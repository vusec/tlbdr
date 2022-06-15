# TLB;DR case study: PTHammer

Artifacts for the PTHammer optimized eviction sets case study.
This work is part of [TLB;DR](https://vusec.net/projects/tlbdr).

## Contents
### Data
- `results/r4.*.tct` — raw measurement data we used in the paper
### Experiment Code
- `ptsim/` — hammer rate measuring tool; see its own README for details
### Scripts
- `plot.py` — produce PDF (or PGF) graphs as used in the paper
### Utils
- `plotham.py` — low-level implementation of plotting

## How to
### Reproduce graphs
- Run `plot.py`

### Re-run experiments / customize
- See the README under `ptsim/`
