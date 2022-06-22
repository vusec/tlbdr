# TLB - PCID

This tool detects whether PCID slots are shared between hyperthreads. This work is part of [TLB;DR](https://vusec.net/tlbdr).

## Running
Before building and running, you need to configure the cores in `./mmuctl/source/kmod.c`.
To test properly, the macros `CORE1` and `CORE2` have to be set to logical cores that are co-resident, i.e.
they are on the same physical core and share the TLB.

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
cd ../; ./prepare.sh; cd PCID
```

to install them.

## Sample output
The output can be seen using the `dmesg` command. A sample output of `./trigger`:

```
Success with DTLB is 1000
Success with STLB is 1000
Success with ITLB is 1000
```
