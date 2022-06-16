# TLB;DR case study: TLBleed covert channel

A mockup TLB covert channel implementation, measuring maximum throughput using different eviction strategies.
This work is part of [TLB;DR](https://vusec.net/projects/tlbdr).

## How to
### Reproduce results
WARNING: microarchitectural covert channels are very finnicky and this is proof-of-concept code; YMMV and multiple executions and/or manual tuning may be required to get meaningful results on your system.
Tune the `TIMETHRESH_*` constants at the start of `covert-channel-tlb.c` if needed.

1. Ensure you are running on an Intel Kaby Lake (model number 7xxx) CPU and have a C compiler and build tools installed
	- (optional) set aside for the experiment one core, isolated from the rest of the system e.g., using cpusets
1. Run `make` to build the experiment binaries
1. Run `covert-naive` and `covert-ninja` successively by hand, providing as argument the core to run on
	1. Monitor the sanity check "power level indicator" at the beginning of each execution
		- `SET ON` should hover around 50 for naive, 60 for ninja
		- `SET OFF` should be near 0 for naive, 20 for ninja
		- If power too high lower threshold values
		- If power too low raise threshold values
	1. Once sanity check looks OK, let experiment run for a few seconds
		- If there are many errors or no output at all after ~5 sec, the covert channel cannot synchronize and must be restarted
	1. If all OK you get a measurement of bandwitdh and error rate
		- Sanity check if measurements are within expected ranges—if not, tune the thresholds more
		- Vary the thresholds by small amounts to ensure you found the optimal value
