#!/bin/bash

# TWEAKABLES
# Pair of co-resident logical cores to run the experiments on
# Recommended to isolate them from the rest of the scheduler w/ cpusets
RECVCORE=2
SENDCORE=6

# END TWEAKABLES

set -e

cmfile="clear-max.txt"
cpfile="clear-probe.txt"
umfile="inuse-max.txt"
upfile="inuse-probe.txt"

function madit {
	taskset -c $RECVCORE ./madtlb $1 2>> $2
}

function madrun_max {
	madit -rn1 $1
	madit -rn2 $1
	madit -rj1 $1
	madit -rj2 $1
}

function madrun_probe {
	madit "-Pn1 /dev/null 7 86" $1
	madit "-Pn2 /dev/null 7 86" $1
	madit "-Pj1 /dev/null 7 86" $1
	madit "-Pj2 /dev/null 7 86" $1
	madit "-Sn /dev/null 7 86" $1
	madit "-Sj /dev/null 7 86" $1
}

echo "Measuring clear channel..."
madrun_max "$cmfile"
madrun_probe "$cpfile"

echo "Measuring in-use channel..."
taskset -c $SENDCORE ./madtlb -s 2>> /dev/null &
sendpid=$!

madrun_max "$umfile"
madrun_probe "$upfile"

kill $sendpid
