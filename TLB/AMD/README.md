# TLB - AMD

This tool detects TLB properties such as its layout and hash functions for TLBs without a shared L2 TLB / sTLB, such as AMD TLBs. This work is part of [TLB;DR](https://vusec.net/tlbdr).

## Running
To build, run

```
make
```

For first time usage after reboot, insert module
```
sudo insmod mmuctl/mmuctl.ko
```

To start testing, run (as root)

```
./trigger
```

Alternatively, build, insert & run with default options 

```
make test
```

In case you don't have the prerequisites installed, you can run

```
./prepare.sh
```

to install them.

## Arguments

| Argument  | Explanation |
| ------------- | ------------- |
| --iterations  | The number of times the tests should be repeated (default = 1000).  |
| --itlb-set  | Which ITLB set to test the replacement policies in.  |
| --dtlb-set  | Which DTLB set to test the replacement policies in.  |
| --stress  | Instead of disabling the second co-resident core, enable it and stress on it.  |
| --hyperthreading  | Instead of disabling the second co-resident core, enable it.  |
| --core  | Pin the tool at a specific core (default = 0).  |
| --test  | Start a specific test. Currently only available when running for the second time.  |

## Output
The output is partially redirected to the userspace process, and is simply printed on your screen.
More details of the experiment results be found using the command `dmesg`.
