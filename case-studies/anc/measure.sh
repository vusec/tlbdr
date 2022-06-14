#!/bin/bash

# TWEAKABLES

NMEAS=${1:-20}	# No. of measurements
NRUNS=${2:-10}	# Runs per measurement

PATCH=./revanc-ninja-evsets.patch
NPATCH=./revanc-ninja-enable.patch

# END TWEAKABLES

set -e

echo "Doing $NMEAS measurements of $NRUNS ASLR breaks each"

time=`which time`
nvout=./anc-naive-$NRUNS.txt
njout=./anc-ninja-$NRUNS.txt

test -x $time || (echo "time binary not found" && exit 1)
test -e $PATCH || (echo "patch '$PATCH' not found" && exit 1)

test -d ./revanc/.git && (git -C ./revanc restore '*' && git -C ./revanc clean -df) || git clone https://github.com/vusec/revanc.git

git -C ./revanc apply - < $PATCH
make -C ./revanc

rm -f $nvout $njout
echo -e "\nMeasuring vanilla AnC..."
for i  in `seq $NMEAS`; do echo -n "$i "; $time -f %e -ao ./anc-naive-$NRUNS.txt ./revanc/obj/anc -n $NRUNS >/dev/null; done

make -C ./revanc clean
git -C ./revanc apply - < $NPATCH
make -C ./revanc

echo -e "\nMeasuring ninja AnC..."
for i  in `seq $NMEAS`; do echo -n "$i "; $time -f %e -ao ./anc-ninja-$NRUNS.txt ./revanc/obj/anc -n $NRUNS >/dev/null; done
echo
