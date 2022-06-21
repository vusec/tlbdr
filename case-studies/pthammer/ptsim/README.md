# TLB;DR case study: PTHammer: ptsim

Tool to measure the hammer rate of a PTHammer-style attack using different TLB eviction strategies.
This work is part of [TLB;DR](https://vusec.net/projects/tlbdr).

## Contents
- `mmuctl/` — kernel module to access page tables (used to determine the physical address of a target page's PTE)
- `ptham.c` — main hammer rate measurement tool
- `chkbk.py` — helper utility to select DRAM-bank-conflicting PTE addresses
- `bkmap.txt` — data file for `chkbk.py` listing least-significant bit patterns of PTE physical addresses that produce DRAM bank conflicts (as determined on a Kaby Lake i7-7700K with 32 GiB of dual-channel, dual-rank, single-DIMM DDR4 memory).
- `run.sh` — convenience script to set up environment and run `ptham`
- `filter.sh` — convenience script used to filter output down to the fields measuring hammer rate

## How to
### Build
1. Ensure you have a C compiler and the Linux kernel headers installed
1. Run `make`

### Run
1. Ensure you have Python 3 installed and a writable `/tmp/` directory
	- (optional) review the constants at the start of `run.sh`
1. Allocate at least one 1GiB hugepage via hugetlbfs
	- at runtime: run `echo 1 > /sys/kernel/mm/hugepages/hugepages-1048576kB/nr_hugepages` as root
	- at boot: add `hugepagesz=1G hugepages=1` to the kernel command line
1. (optional) Disable dynamic frequency scaling (e.g., `cpupower frequency-set -g performance`),
1. Run `make run` (or `run.sh` directly)
1. Monitor the results directory for progress beyond 1KiB for each file (tool hangs in some edge cases and needs to be manually restarted)
	- if a measurement hangs, terminate (via `SIGTERM`) the `ptham` process and `run.sh` will automatically restart that measurement
	- interrupting (`^C`) `ptham` or `run.sh` will terminate the entire experiment run
1. Sanity check the first result file
	- `Flush` and `CacheEv` timings should be roughly similar
	- all other measurements should be much larger than `Normal timing run`
	- the *first* measurement lines of `Naive(T) PC` and `Ninja(T) PC` should be roughly similar to `Naive(T)`
	- the *first* measurement lines of `Naive(T+C) PC` and `Ninja(T+C) PC` should be roughly similar `Naive(T+C)`
1. (sanity checks pass) Continue monitoring results until finished.
	- (alternatively) Interrupt `make run`, remove `ptham` and re-invoke with `CFLAGS=-DNOSANITY make run` to run faster measurements without the sanity checks.
1. (sanity checks fail) Something is wrongly configured, see following checklist.
	- `Flush != CacheEv`: Cache eviction failure. Possibly wrong cache set functions.
	- other measurements as low as `Normal timing run`: TLB eviction failure. Possibly wrong TLB set functions.
	- `*(T) PC` have lower values: Failure in TLB pointer chasing. Check `tlb_prep*` functions.
	- `*(T+C) PC` have lower values: Failure in cache pointer chasing. Check `cache_prep*` functions.


### Get plottable results
1. Ensure you have obtained results by running the tool
	- (optional) review the default results directory in `filter.sh`
1. Run `filter.sh`

### Customize
- `run.sh` & `filter.sh` have tweakable constants declared within the first lines
- `bkmap.txt` can be generated for another machine by timing pairs of uncached memory accesses and selecting those with clearly higher latency
- `ptham.c` assumes a Kaby Lake CPU and some engineering is required to make it work on other machines
