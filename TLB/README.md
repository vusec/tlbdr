# TLB

This tool detects TLB properties such as its layout, its hash functions and its replacement policies. This work is part of [TLB;DR](https://vusec.net/tlbdr).  
For TLBs without a shared L2/sTLB, such as AMD Zen+ and AMD Zen 3, the tool in `./AMD` can be used.  
To test whether PCID entries are shared across hyperthreads, the tool in `./PCID` can be used.

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
| --set-distribuion  | Shows how many failures each set had during the testing of replacement policies/permutation vectors.  |
| --sequence  | Show the sequences used for testing the replacement policies.  |
| --iterations  | The number of times the tests should be repeated (default = 1000).  |
| --stlb-set  | Which STLB set to test the replacement policies in.  |
| --itlb-set  | Which ITLB set to test the replacement policies in.  |
| --dtlb-set  | Which DTLB set to test the replacement policies in.  |
| --stress  | Instead of disabling the second co-resident core, enable it and stress on it.  |
| --hyperthreading  | Instead of disabling the second co-resident core, enable it.  |
| --core  | Pin the tool at a specific core (default = 0).  |
| --test  | Start a specific test. Currently only available when running for the second time.  |

## Sample output
A sample output of `./trigger --set-distribution`:

```
This tool tests TLB properties. It will disable all but one core per physical core. In addition, kernel preemption and interrupts will be disabled while testing. YOU MAY LOSE CONTROL OVER YOUR MACHINE DURING TESTING. Please save important work before proceeding. Do you want to continue [y|n]?
y
Pinned on core 0, disabled core 2.

1. iTLB and dTLB are non-inclusive of sTLB: Yes (success rate 1000 / 1000).

2. sTLB is non-exclusive of iTLB and dTLB: Yes (success rate 1000 / 1000).

3. sTLB hash function: XOR-7 hash function (128 sets), 12 ways/set.

4. iTLB hash function: LIN-16 hash function (16 sets), 8 ways/set.

5. dTLB hash function: LIN-16 hash function (16 sets), 4 ways/set.

6. iTLB re-insertion upon sTLB hit: Yes (success rate 1000 / 1000).

7. dTLB re-insertion upon sTLB hit: Yes (success rate 1000 / 1000).

8. sTLB re-insertion upon L1 hit: No (success rate data 0 / 1000, success rate instruction 0 / 1000).

9. sTLB re-insertion upon L1 eviction: No (success rate data 0 / 1000, success rate instruction 0 / 1000).

10. sTLB replacement policy: (MRU+1)%3PLRU4 short sequence evicted 0 with success 994 / 1000, long sequence did not evict 0 with success 998 / 1000.
Set 1: 1 failures out of 13 tries
Set 83: 3 failures out of 10 tries
Set 93: 1 failures out of 16 tries
Set 123: 3 failures out of 9 tries

11. iTLB replacement policy: PLRU policy short sequence evicted 0 with success 939 / 1000, long sequence did not evict 0 with success 853 / 1000.
Set 0: 67 failures out of 119 tries
Set 1: 7 failures out of 126 tries
Set 2: 13 failures out of 143 tries
Set 4: 7 failures out of 121 tries
Set 5: 2 failures out of 137 tries
Set 6: 1 failures out of 128 tries
Set 7: 7 failures out of 113 tries
Set 8: 6 failures out of 130 tries
Set 9: 5 failures out of 137 tries
Set 10: 3 failures out of 126 tries
Set 11: 58 failures out of 127 tries
Set 12: 10 failures out of 111 tries
Set 13: 2 failures out of 122 tries
Set 14: 15 failures out of 129 tries
Set 15: 5 failures out of 115 tries

12. dTLB replacement policy: PLRU short sequence evicted 0 with success 1000 / 1000, long sequence did not evict 0 with success 934 / 1000.
Set 3: 56 failures out of 133 tries
Set 4: 3 failures out of 100 tries
Set 5: 7 failures out of 138 tries

13. sTLB permutation vectors: 
Testing for miss vector: accessing 12 addresses resulted in all of them being in the set 936 / 1000
π0: 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 (agreement 11992 / 12000)
π1: 1, 2, 0, 4, 5, 3, 7, 8, 6, 10, 11, 9 (agreement 11991 / 12000)
π2: 2, 0, 1, 5, 3, 4, 8, 6, 7, 11, 9, 10 (agreement 11991 / 12000)
π3: 3, 1, 2, 0, 4, 5, 9, 7, 8, 6, 10, 11 (agreement 11990 / 12000)
π4: 4, 2, 0, 1, 5, 3, 10, 8, 6, 7, 11, 9 (agreement 11991 / 12000)
π5: 5, 0, 1, 2, 3, 4, 11, 6, 7, 8, 9, 10 (agreement 11991 / 12000)
π6: 6, 1, 2, 3, 4, 5, 0, 7, 8, 9, 10, 11 (agreement 11994 / 12000)
π7: 7, 2, 0, 4, 5, 3, 1, 8, 6, 10, 11, 9 (agreement 11988 / 12000)
π8: 8, 0, 1, 5, 3, 4, 2, 6, 7, 11, 9, 10 (agreement 11996 / 12000)
π9: 9, 1, 2, 0, 4, 5, 3, 7, 8, 6, 10, 11 (agreement 11993 / 12000)
π10: 10, 2, 0, 1, 5, 3, 4, 8, 6, 7, 11, 9 (agreement 11990 / 12000)
π11: 11, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 (agreement 11996 / 12000)
Set 1: 2 (-1)
	Total 2 mistakes out of 1088 attempts
Set 2: 18 (-1)
	Total 18 mistakes out of 1134 attempts
Set 4: 2 (-1)
	Total 2 mistakes out of 1153 attempts
Set 6: 1 (-1)
	Total 1 mistakes out of 1063 attempts
Set 10: 3 (-1)
	Total 3 mistakes out of 1133 attempts
Set 12: 1 (-1)
	Total 1 mistakes out of 1107 attempts
Set 13: 2 (-1)
	Total 2 mistakes out of 1131 attempts
Set 14: 10 (-1)
	Total 10 mistakes out of 1081 attempts
Set 18: 1 (-1)
	Total 1 mistakes out of 1093 attempts
Set 27: 1 (-1)
	Total 1 mistakes out of 1081 attempts
Set 30: 9 (-1)
	Total 9 mistakes out of 1150 attempts
Set 31: 19 (-1)
	Total 19 mistakes out of 1085 attempts
Set 34: 1 (-3)
	Total 1 mistakes out of 1070 attempts
Set 36: 1 (-2)
	Total 1 mistakes out of 1091 attempts
Set 49: 6 (-1)
	Total 6 mistakes out of 1141 attempts
Set 65: 3 (-1)
	Total 3 mistakes out of 1060 attempts
Set 68: 1 (-1)
	Total 1 mistakes out of 1158 attempts
Set 71: 6 (-1)
	Total 6 mistakes out of 1150 attempts
Set 79: 5 (-1)
	Total 5 mistakes out of 1176 attempts
Set 82: 3 (-1)
	Total 3 mistakes out of 1140 attempts
Set 88: 1 (-1)
	Total 1 mistakes out of 1106 attempts
Set 125: 1 (-1)
	Total 1 mistakes out of 1208 attempts
Total mistakes: 97
Total attempts: 144000

14. dTLB permutation vectors: 
Testing for miss vector: accessing 4 addresses resulted in all of them being in the set 922 / 1000
π0: 0, 1, 2, 3 (agreement 3801 / 4000)
π1: 1, 0, 3, 2 (agreement 3678 / 4000)
π2: 2, 1, 0, 3 (agreement 3688 / 4000)
π3: 3, 0, 1, 2 (agreement 3693 / 4000)
Set 0: 99 (-1) 49 (-2) 139 (+1) 50 (+2)
	Total 337 mistakes out of 991 attempts
Set 1: 2 (-1)
	Total 2 mistakes out of 965 attempts
Set 3: 285 (-1) 480 (-2) 15 (-3) 3 (+1)
	Total 783 mistakes out of 1044 attempts
Set 4: 3 (-1) 1 (NE)
	Total 4 mistakes out of 992 attempts
Set 5: 1 (-1)
	Total 1 mistakes out of 1020 attempts
Set 8: 2 (-1)
	Total 2 mistakes out of 1011 attempts
Set 9: 1 (-1)
	Total 1 mistakes out of 979 attempts
Set 10: 1 (-3)
	Total 1 mistakes out of 1004 attempts
Set 12: 1 (-1)
	Total 1 mistakes out of 1000 attempts
Set 14: 4 (-1) 1 (-2)
	Total 5 mistakes out of 1004 attempts
Set 15: 2 (-1) 1 (+1)
	Total 3 mistakes out of 1010 attempts
Total mistakes: 1140
Total attempts: 16000

15. iTLB permutation vectors: 
Testing for miss vector: accessing 8 addresses resulted in all of them being in the set 870 / 1000
π0: 0, 1, 2, 3, 4, 5, 6, 7 (agreement 5735 / 8000)
π1: 1, 0, 3, 2, 5, 4, 7, 6 (agreement 5861 / 8000)
π2: 2, 1, 0, 3, 6, 5, 4, 7 (agreement 5805 / 8000)
π3: 3, 0, 1, 2, 7, 4, 5, 6 (agreement 5976 / 8000)
π4: 4, 1, 2, 3, 0, 5, 6, 7 (agreement 5878 / 8000)
π5: 5, 0, 3, 2, 1, 4, 7, 6 (agreement 5847 / 8000)
π6: 6, 1, 0, 3, 2, 5, 4, 7 (agreement 5788 / 8000)
π7: 7, 0, 1, 2, 3, 4, 5, 6 (agreement 5903 / 8000)
Set 0: 570 (-1) 534 (-2) 435 (-3) 1741 (-4) 226 (-5)
	Total 3506 mistakes out of 3992 attempts
Set 1: 2613 (-1) 20 (-2) 9 (-3) 1 (-4) 1 (-5) 3 (+1)
	Total 2647 mistakes out of 3945 attempts
Set 2: 678 (-1) 473 (-2) 509 (-3) 443 (-4) 32 (+1) 11 (+2) 23 (+3) 7 (+4) 10 (+5) 1323 (NE)
	Total 3509 mistakes out of 4046 attempts
Set 3: 578 (-1) 608 (-2) 465 (-3) 1713 (-4) 250 (-5)
	Total 3614 mistakes out of 4139 attempts
Set 4: 15 (-1) 3 (-2) 6 (-3) 1 (+1)
	Total 25 mistakes out of 4045 attempts
Set 5: 721 (-1) 480 (-2) 479 (-3) 417 (-4) 53 (+1) 7 (+2) 39 (+3) 10 (+4) 5 (+5) 1281 (NE)
	Total 3492 mistakes out of 3990 attempts
Set 6: 8 (-1) 1 (-2) 2 (-3) 3 (+1)
	Total 14 mistakes out of 3962 attempts
Set 7: 50 (-1) 1 (-2) 2 (-3)
	Total 53 mistakes out of 3954 attempts
Set 9: 21 (-1) 2 (-2) 2 (-3) 1 (+1)
	Total 26 mistakes out of 4007 attempts
Set 10: 9 (-1) 1 (+1)
	Total 10 mistakes out of 3996 attempts
Set 11: 109 (-1) 6 (-2) 5 (-3) 8 (+1)
	Total 128 mistakes out of 3978 attempts
Set 12: 23 (-1) 6 (-2) 3 (-3) 4 (+1)
	Total 36 mistakes out of 4049 attempts
Set 13: 18 (-1) 14 (-2) 2 (-3) 2 (-4) 2 (+1)
	Total 38 mistakes out of 3955 attempts
Set 14: 45 (-1) 3 (-2) 4 (-3) 7 (+1)
	Total 59 mistakes out of 3973 attempts
Set 15: 48 (-1) 2 (-3)
	Total 50 mistakes out of 3942 attempts
Total mistakes: 17207
Total attempts: 64000

16. STLB PCID limit: 4 (with the NOFLUSH bit: 4)
Distribution:
0 PCIDs: 0 / 1000.
1 PCIDs: 0 / 1000.
2 PCIDs: 0 / 1000.
3 PCIDs: 0 / 1000.
4 PCIDs: 1000 / 1000.
Distribution NOFLUSH:
0 PCIDs: 0 / 1000.
1 PCIDs: 0 / 1000.
2 PCIDs: 0 / 1000.
3 PCIDs: 0 / 1000.
4 PCIDs: 1000 / 1000.

17. dTLB PCID limit: 0 (with the NOFLUSH bit: 0).
Distribution:
0 PCIDs: 1000 / 1000.
Distribution NOFLUSH:
0 PCIDs: 1000 / 1000.

18. iTLB PCID limit: 1 (with the NOFLUSH bit: 3).
Distribution:
0 PCIDs: 0 / 1000.
1 PCIDs: 1000 / 1000.
Distribution NOFLUSH:
0 PCIDs: 0 / 1000.
1 PCIDs: 989 / 1000.
2 PCIDs: 993 / 1000.
3 PCIDs: 1000 / 1000.

19. sTLB PCID permutation vectors: 
π0: 0, 1, 2, 3 (agreement 20 / 20)
π1: 1, 0, 2, 3 (agreement 20 / 20)
π2: 2, 0, 1, 3 (agreement 20 / 20)
π3: 3, 0, 1, 2 (agreement 20 / 20)
sTLB PCID permutation vectors NOFLUSH: 
π0: 0, 1, 2, 3 (agreement 19 / 20)
π1: 1, 0, 2, 3 (agreement 19 / 20)
π2: 2, 0, 1, 3 (agreement 20 / 20)
π3: 3, 0, 1, 2 (agreement 20 / 20)

Enabled all cores.
```
