#!/usr/bin/env python3
# -*- coding: UTF-8 -*-

import sys

from collections import defaultdict

BKs = defaultdict(set)

with open('bkmap.txt', 'r') as f:
	for a,b in ([int(y, 16) for y in x.split(':')[:2]] for x in f):
		BKs[a].add(b)


def chk(a, b):
	return max(a,b) in BKs[min(a,b)]


def pr1st():
	avail = [int(x, 16) for x in sys.stdin]
	for i in range(len(avail)):
		for j in range(i+1, len(avail)):
			if chk(avail[i], avail[j]):
				print(i, j)
				return
	print('Nope')


if __name__ == '__main__':
	pr1st()
