# TLB;DR case study: TLBleed

Proof-of-concept code to measure the effect of optimal eviction sets on TLBleed-style attacks.
This work is part of [TLB;DR](https://vusec.net/projects/tlbdr).

## Contents
- `madtlb.c` — utility to probe TLB sets using different strategies; used for measuring raw sample rates
- `measure.sh` — script automating raw sample rate collection
- `covert-chan/` — covert channel mockup implementation; see its own README for details

## How to
### Reproduce results
(exec time: ~10 min)
1. Ensure you are running on an Intel Kaby Lake (model number 7xxx) CPU with Hyperthreading enabled and have a C compiler and build tools installed
	- (optional) set aside for the experiment the two threads of one core, isolated from the rest of the system e.g., using cpusets; ensure these match the ones defined at the beginning of `measure.sh`
1. Run `make run` (or `make` then `measure.sh` directly) which will produce several output files:
	- `clear-*.txt` are measurements on a quiescent system
	- `inuse-*.txt` are measurements with an active sender running
	- `*-max.txt` are maximum possible rates, with no storing/processing of the actual samples
	- `*-probe.txt` are more realistic values, with the receiver summarily processing samples
