#!/bin/bash

set -e

RESDIR="results"


for i in "$RESDIR"/*; do
	nfn=${i/.txt/.tct}
	grep -A2 '^N....(T+C)' "$i" > "$nfn"
done
