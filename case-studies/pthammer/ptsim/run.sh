#!/bin/bash

set -e

TMPFILE="/tmp/pth"
PIPE="/tmp/pth-pipe"
RESDIR="results"
ITERS=5

test 0 -eq `cat /sys/kernel/mm/hugepages/hugepages-1048576kB/nr_hugepages` && echo "No 1G hugepages available, aborting." && exit 1

run_ptham () {
	rm -f "$TMPFILE" "$PIPE"
	mkfifo "$PIPE"

	echo "Running iteration $1"

	cat "$PIPE" | LD_LIBRARY_PATH="$PWD/mmuctl" stdbuf -oL -eL ./ptham > "$TMPFILE" 2> "$RESDIR/r.$1.txt" &

	trap "kill $!" INT
	echo "Waiting for hammer candidates..."
	while test ! -s "$TMPFILE"; do sleep 1; done

	ans=`./chkbk.py < "$TMPFILE"`

	test -z "$ans" -o "$ans" = "Nope" && kill %% && echo "Hammer pair not found, trying again..." && return 1

	echo "$ans" > "$PIPE"
	echo "Hammer candidates found, continuing..."
	wait -n
	rv=$?
	trap - INT
	test $rv -eq 130 && return 2
	return $rv
}


mkdir -p "$RESDIR"

echo "Loading module..."
sudo rmmod mmuctl || true
sudo insmod mmuctl/mmuctl.ko

echo "Running ptham..."
i=0
while test $i -lt "$ITERS"; do
	run_ptham $i || if [ $? -eq 2 ]; then break; else continue; fi
	echo $?
	i=$(($i + 1))
done

wait
echo "Finished, cleaning up..."
rm -f "$TMPFILE" "$PIPE"
sudo rmmod mmuctl || true
